#pragma once

#include "pare_ta_resource.hpp"
#include <iostream>
#include <memory>
#include <vector>
#include <cstdint>

#if __has_include("onnxruntime_c_api.h")
#include "onnxruntime_c_api.h"
#include "onnxruntime_cxx_api.h"
#define PARE_TA_HAS_ONNX 1
#elif __has_include("../yolo26n_pipeline/onnxruntime/include/onnxruntime_cxx_api.h")
#include "../yolo26n_pipeline/onnxruntime/include/onnxruntime_cxx_api.h"
#define PARE_TA_HAS_ONNX 1
#else
#define PARE_TA_HAS_ONNX 0
#endif

namespace pare_ta {

#if PARE_TA_HAS_ONNX

// ============================================================================
// PARE-TA ONNX Runtime Custom Allocator Adapter
// ============================================================================
// Implements the native OrtAllocator interface for ONNX Runtime C/C++ API.
// Directs all tensor memory traffic (weights + activation scratchpads) into
// the Page-Aligned Monotonic Resource.
class PareTaOrtAllocator : public OrtAllocator {
private:
    std::shared_ptr<PareTaMonotonicResource> resource_;
    OrtMemoryInfo* mem_info_{nullptr};

public:
    explicit PareTaOrtAllocator(std::shared_ptr<PareTaMonotonicResource> resource)
        : resource_(std::move(resource))
    {
        version = ORT_API_VERSION;
        Alloc = AllocStatic;
        Free = FreeStatic;
        Info = InfoStatic;
        Reserve = ReserveStatic;

        const auto& api = Ort::GetApi();
        auto status = api.CreateCpuMemoryInfo(OrtDeviceAllocator, OrtMemTypeDefault, &mem_info_);
        if (status) {
            std::cerr << "[PARE-TA] Failed to create CPU memory info: "
                      << api.GetErrorMessage(status) << std::endl;
            api.ReleaseStatus(status);
        }
    }

    ~PareTaOrtAllocator() {
        if (mem_info_) {
            Ort::GetApi().ReleaseMemoryInfo(mem_info_);
        }
    }

    PareTaMonotonicResource* get_resource() const noexcept {
        return resource_.get();
    }

    void begin_frame() noexcept {
        resource_->begin_frame();
    }

    void end_frame() noexcept {
        resource_->end_frame();
    }

    void set_mode_persistent() noexcept {
        resource_->set_tier(PareTaMonotonicResource::Tier::PERSISTENT);
    }

    void set_mode_transient() noexcept {
        resource_->set_tier(PareTaMonotonicResource::Tier::TRANSIENT);
    }

private:
    static void* ORT_API_CALL AllocStatic(struct OrtAllocator* this_, size_t size) {
        auto* self = static_cast<PareTaOrtAllocator*>(this_);
        // Large tensors (>= 4 KiB) align to page boundary for zero trapped DRAM.
        // Smaller tensors align to 64-byte hardware cache lines.
        bool align_page = (size >= PAGE_SIZE_4K);
        return self->resource_->allocate_tensor(size, align_page);
    }

    static void ORT_API_CALL FreeStatic(struct OrtAllocator* this_, void* p) {
        if (!p) return;
        auto* self = static_cast<PareTaOrtAllocator*>(this_);
        // In PARE-TA, individual tensor deallocation is an O(1) no-op!
        // Memory is reclaimed in bulk at the frame boundary via end_frame().
        self->resource_->deallocate(p, 0);
    }

    static const struct OrtMemoryInfo* ORT_API_CALL InfoStatic(const struct OrtAllocator* this_) {
        const auto* self = static_cast<const PareTaOrtAllocator*>(this_);
        return self->mem_info_;
    }

    static void* ORT_API_CALL ReserveStatic(struct OrtAllocator* this_, size_t size) {
        return AllocStatic(this_, size);
    }
};

#endif // PARE_TA_HAS_ONNX

// ============================================================================
// PMR Tensor Types for Standard C++ CV Pipelines (LibTorch, OpenCV, Dataloaders)
// ============================================================================
template <typename T>
using PmrVector = std::pmr::vector<T>;

template <typename T = float>
class PmrTensor2D {
public:
    size_t rows{0};
    size_t cols{0};
    PmrVector<T> data;

    PmrTensor2D(size_t r, size_t c, std::pmr::memory_resource* mr)
        : rows(r), cols(c), data(r * c, mr) {}

    T* ptr() noexcept { return data.data(); }
    const T* ptr() const noexcept { return data.data(); }
    size_t size_bytes() const noexcept { return rows * cols * sizeof(T); }
};

} // namespace pare_ta
