#pragma once

#include <iostream>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <atomic>
#include <sys/mman.h>
#include <unistd.h>
// #include "onnxruntime_c_api.h"
#include "../yolo26n_pipeline/onnxruntime/include/onnxruntime_cxx_api.h"

namespace factory_cv {

class OrtDoubleBufferedArena : public OrtAllocator {
public:
    enum class AllocMode {
        INITIALIZER, // Model weights and persistent metadata
        TRANSIENT    // Per-frame tensor activations
    };

private:
    OrtMemoryInfo* mem_info_;
    AllocMode mode_{AllocMode::INITIALIZER};

    // Tier 1: Static Weights Arena (Permanent, 64 MB virtual reservation)
    uint8_t* weights_buffer_{nullptr};
    size_t weights_capacity_{64 * 1024 * 1024};
    size_t weights_offset_{0};

    // Tier 2: Double-Buffered Frame Transient Arena (Ping-Pong)
    // 2 x 128 MB contiguous physical page-aligned buffers
    uint8_t* frame_buffer_a_{nullptr};
    uint8_t* frame_buffer_b_{nullptr};
    size_t frame_capacity_{128 * 1024 * 1024};
    size_t frame_offset_{0};
    int active_buffer_idx_{0}; // 0 = A, 1 = B

    // Diagnostic telemetry counters
    std::atomic<size_t> weight_allocs_{0};
    std::atomic<size_t> weight_bytes_{0};
    std::atomic<size_t> transient_allocs_{0};
    std::atomic<size_t> transient_bytes_{0};
    std::atomic<size_t> noop_frees_{0};
    size_t peak_frame_offset_{0};

public:
    OrtDoubleBufferedArena(size_t weights_mb = 64, size_t frame_mb = 128)
        : weights_capacity_(weights_mb * 1024 * 1024),
          frame_capacity_(frame_mb * 1024 * 1024) 
    {
        version = ORT_API_VERSION;
        Alloc = AllocStatic;
        Free = FreeStatic;
        Info = InfoStatic;
        Reserve = ReserveStatic;

        const auto& api = Ort::GetApi();
        auto status = api.CreateCpuMemoryInfo(OrtDeviceAllocator, OrtMemTypeDefault, &mem_info_);
        if (status) {
            std::cerr << "Failed to create cpu memory info: " << api.GetErrorMessage(status) << std::endl;
            api.ReleaseStatus(status);
        }

        // Allocate 64-byte aligned, page-locked virtual buffers via posix_memalign
        int ret = posix_memalign(reinterpret_cast<void**>(&weights_buffer_), 4096, weights_capacity_);
        if (ret != 0 || !weights_buffer_) {
            throw std::runtime_error("Failed to allocate weights_buffer_");
        }
        std::memset(weights_buffer_, 0, 4096); // Pre-fault initial page

        ret = posix_memalign(reinterpret_cast<void**>(&frame_buffer_a_), 4096, frame_capacity_);
        if (ret != 0 || !frame_buffer_a_) {
            throw std::runtime_error("Failed to allocate frame_buffer_a_");
        }
        std::memset(frame_buffer_a_, 0, 4096);

        ret = posix_memalign(reinterpret_cast<void**>(&frame_buffer_b_), 4096, frame_capacity_);
        if (ret != 0 || !frame_buffer_b_) {
            throw std::runtime_error("Failed to allocate frame_buffer_b_");
        }
        std::memset(frame_buffer_b_, 0, 4096);
    }

    ~OrtDoubleBufferedArena() {
        if (weights_buffer_) free(weights_buffer_);
        if (frame_buffer_a_) free(frame_buffer_a_);
        if (frame_buffer_b_) free(frame_buffer_b_);
        if (mem_info_) Ort::GetApi().ReleaseMemoryInfo(mem_info_);
    }

    void set_mode(AllocMode m) {
        mode_ = m;
    }

    void begin_frame() {
        mode_ = AllocMode::TRANSIENT;
        frame_offset_ = 0;
    }

    void end_frame() {
        if (frame_offset_ > peak_frame_offset_) {
            peak_frame_offset_ = frame_offset_;
        }
        // Ping-pong buffer swap
        active_buffer_idx_ ^= 1;
        frame_offset_ = 0;
    }

    size_t get_weight_allocs() const { return weight_allocs_.load(); }
    size_t get_weight_bytes() const { return weight_bytes_.load(); }
    size_t get_transient_allocs() const { return transient_allocs_.load(); }
    size_t get_transient_bytes() const { return transient_bytes_.load(); }
    size_t get_noop_frees() const { return noop_frees_.load(); }
    size_t get_peak_frame_bytes() const { return peak_frame_offset_; }

private:
    static void* ORT_API_CALL AllocStatic(struct OrtAllocator* this_, size_t size) {
        auto* self = static_cast<OrtDoubleBufferedArena*>(this_);
        // 64-byte alignment for SIMD/AVX-512/NEON vectorization
        size_t aligned_size = (size + 63) & ~size_t(63);

        if (self->mode_ == AllocMode::INITIALIZER) {
            self->weight_allocs_++;
            self->weight_bytes_ += aligned_size;
            if (self->weights_offset_ + aligned_size > self->weights_capacity_) {
                std::cerr << "OrtDoubleBufferedArena: FATAL out of weights arena memory (" 
                          << self->weights_offset_ + aligned_size << " > " << self->weights_capacity_ << ")" << std::endl;
                std::abort();
            }
            void* ptr = self->weights_buffer_ + self->weights_offset_;
            self->weights_offset_ += aligned_size;
            return ptr;
        } else {
            // Transient Frame Bump Allocation
            self->transient_allocs_++;
            self->transient_bytes_ += aligned_size;
            if (self->frame_offset_ + aligned_size > self->frame_capacity_) {
                std::cerr << "OrtDoubleBufferedArena: FATAL out of frame arena memory (" 
                          << self->frame_offset_ + aligned_size << " > " << self->frame_capacity_ << ")" << std::endl;
                std::abort();
            }
            uint8_t* active_buf = (self->active_buffer_idx_ == 0) ? self->frame_buffer_a_ : self->frame_buffer_b_;
            void* ptr = active_buf + self->frame_offset_;
            self->frame_offset_ += aligned_size;
            return ptr;
        }
    }

    static void ORT_API_CALL FreeStatic(struct OrtAllocator* this_, void* p) {
        if (!p) return;
        auto* self = static_cast<OrtDoubleBufferedArena*>(this_);
        // In bump-pointer arena, individual frees are 100% O(1) no-ops.
        // Memory is reclaimed in bulk at frame boundary via end_frame().
        self->noop_frees_++;
    }

    static const struct OrtMemoryInfo* ORT_API_CALL InfoStatic(const struct OrtAllocator* this_) {
        const auto* self = static_cast<const OrtDoubleBufferedArena*>(this_);
        return self->mem_info_;
    }

    static void* ORT_API_CALL ReserveStatic(struct OrtAllocator* this_, size_t size) {
        return AllocStatic(this_, size);
    }
};

} // namespace factory_cv
