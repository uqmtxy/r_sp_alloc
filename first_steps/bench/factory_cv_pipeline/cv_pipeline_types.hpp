#ifndef FACTORY_CV_PIPELINE_TYPES_HPP
#define FACTORY_CV_PIPELINE_TYPES_HPP

#include <cstddef>
#include <cstdint>

namespace factory_cv {

// Pipeline resolution and buffer specifications
constexpr size_t FRAME_WIDTH = 1920;
constexpr size_t FRAME_HEIGHT = 1080;
constexpr size_t FRAME_CHANNELS = 3;
constexpr size_t RAW_FRAME_SIZE = FRAME_WIDTH * FRAME_HEIGHT * FRAME_CHANNELS; // 6,220,800 bytes (~5.93 MiB)

constexpr size_t TENSOR_CHANNELS = 3;
constexpr size_t TENSOR_HEIGHT = 640;
constexpr size_t TENSOR_WIDTH = 640;
constexpr size_t PREPROCESSED_TENSOR_SIZE = TENSOR_CHANNELS * TENSOR_HEIGHT * TENSOR_WIDTH * sizeof(float); // 4,915,200 bytes (~4.69 MiB)

// Neural network intermediate feature activations (YOLOv8/ResNet backbone stages)
constexpr size_t ACTIVATION_STAGE1_SIZE = 1 * 64 * 160 * 160 * sizeof(float); // 6,553,600 bytes (~6.25 MiB)
constexpr size_t ACTIVATION_STAGE2_SIZE = 1 * 128 * 80 * 80 * sizeof(float);  // 3,276,800 bytes (~3.13 MiB)
constexpr size_t ACTIVATION_STAGE3_SIZE = 1 * 256 * 40 * 40 * sizeof(float);  // 1,638,400 bytes (~1.56 MiB)

// Bounding box detection candidates
struct DetectionBox {
    float xmin, ymin, xmax, ymax;
    float confidence;
    int32_t class_id;
    uint32_t defect_flags;
    uint32_t reserved;
}; // 32 bytes

constexpr size_t MAX_DETECTIONS = 300;
constexpr size_t DETECTION_BUFFER_SIZE = MAX_DETECTIONS * sizeof(DetectionBox); // 9,600 bytes

// Persistent model parameters loaded once at system boot
constexpr size_t MODEL_WEIGHTS_SIZE = 120 * 1024 * 1024; // 120 MiB

// Pipeline queue depths (conveyor belt backpressure capacity)
constexpr size_t PIPELINE_QUEUE_CAPACITY = 4;

struct FrameMetadata {
    uint64_t frame_id;
    uint64_t timestamp_ns;
    uint32_t camera_id;
    uint32_t defect_count;
    bool is_defective;
};

} // namespace factory_cv

#endif // FACTORY_CV_PIPELINE_TYPES_HPP
