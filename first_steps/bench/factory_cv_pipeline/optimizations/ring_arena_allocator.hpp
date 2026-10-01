#ifndef FACTORY_CV_RING_ARENA_ALLOCATOR_HPP
#define FACTORY_CV_RING_ARENA_ALLOCATOR_HPP

#include "../cv_pipeline_types.hpp"
#include <vector>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace factory_cv {

// Workspace Buffer Sharing:
// An industrial CV inspection pipeline does not need simultaneous independent physical storage
// for raw camera frame, preprocessed tensor, and inference activations on the SAME frame.
// By implementing ping-pong workspace re-use (as done in TensorRT / TVM memory planning),
// each frame slot requires only two overlapping buffers:
//   Buffer A: max(RAW_FRAME_SIZE, ACTIVATION_STAGE1_SIZE) = 6,553,600 B (~6.25 MiB)
//   Buffer B: max(PREPROCESSED_TENSOR_SIZE, ACTIVATION_STAGE2_SIZE) = 4,915,200 B (~4.69 MiB)
//   Buffer C: ACTIVATION_STAGE3_SIZE = 1,638,400 B (~1.56 MiB)
// Total per slot = 12.5 MiB (down from 22.6 MiB), achieving maximal memory density.

struct FrameSlot {
    uint8_t* raw_frame = nullptr;
    float* preprocessed_tensor = nullptr;
    float* stage1_activation = nullptr;
    float* stage2_activation = nullptr;
    float* stage3_activation = nullptr;
    DetectionBox* detections = nullptr;
    FrameMetadata metadata;
};

class RingArenaAllocator {
public:
    explicit RingArenaAllocator(size_t ring_size = 2)
        : ring_size_(ring_size), slots_(ring_size) {
        
        for (size_t i = 0; i < ring_size_; ++i) {
            // Buffer A: Shared between raw camera frame and stage1 activations
            size_t buf_a_size = std::max(RAW_FRAME_SIZE, ACTIVATION_STAGE1_SIZE);
            void* buf_a = nullptr;
            int r1 = posix_memalign(&buf_a, 64, buf_a_size);
            (void)r1;
            slots_[i].raw_frame = static_cast<uint8_t*>(buf_a);
            slots_[i].stage1_activation = static_cast<float*>(buf_a);

            // Buffer B: Shared between preprocessed tensor and stage2 activations
            size_t buf_b_size = std::max(PREPROCESSED_TENSOR_SIZE, ACTIVATION_STAGE2_SIZE);
            void* buf_b = nullptr;
            int r2 = posix_memalign(&buf_b, 64, buf_b_size);
            (void)r2;
            slots_[i].preprocessed_tensor = static_cast<float*>(buf_b);
            slots_[i].stage2_activation = static_cast<float*>(buf_b);

            // Buffer C: Stage 3 activations
            void* buf_c = nullptr;
            int r3 = posix_memalign(&buf_c, 64, ACTIVATION_STAGE3_SIZE);
            (void)r3;
            slots_[i].stage3_activation = static_cast<float*>(buf_c);

            // Detection buffer
            void* buf_d = nullptr;
            int r4 = posix_memalign(&buf_d, 64, DETECTION_BUFFER_SIZE);
            (void)r4;
            slots_[i].detections = static_cast<DetectionBox*>(buf_d);

            // Pre-fault all physical pages at startup so zero runtime page faults occur
            std::memset(buf_a, 0xAA, buf_a_size);
            std::memset(buf_b, 0, buf_b_size);
            std::memset(buf_c, 0, ACTIVATION_STAGE3_SIZE);
            std::memset(buf_d, 0, DETECTION_BUFFER_SIZE);
        }
    }

    ~RingArenaAllocator() {
        for (auto& slot : slots_) {
            free(slot.raw_frame); // frees Buffer A
            free(slot.preprocessed_tensor); // frees Buffer B
            free(slot.stage3_activation); // frees Buffer C
            free(slot.detections);
        }
    }

    FrameSlot* acquire_slot(uint64_t frame_id) {
        size_t idx = frame_id % ring_size_;
        slots_[idx].metadata.frame_id = frame_id;
        return &slots_[idx];
    }

    void release_slot(FrameSlot* slot) {
        (void)slot;
    }

    size_t total_allocated_bytes() const {
        size_t per_slot = std::max(RAW_FRAME_SIZE, ACTIVATION_STAGE1_SIZE) +
                          std::max(PREPROCESSED_TENSOR_SIZE, ACTIVATION_STAGE2_SIZE) +
                          ACTIVATION_STAGE3_SIZE + DETECTION_BUFFER_SIZE;
        return ring_size_ * per_slot;
    }

private:
    size_t ring_size_;
    std::vector<FrameSlot> slots_;
};

} // namespace factory_cv

#endif // FACTORY_CV_RING_ARENA_ALLOCATOR_HPP
