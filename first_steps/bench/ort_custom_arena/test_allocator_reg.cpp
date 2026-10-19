#include <iostream>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include "onnxruntime_cxx_api.h"
#include "onnxruntime_session_options_config_keys.h"

struct TestCustomAllocator : public OrtAllocator {
    OrtMemoryInfo* mem_info;
    size_t alloc_count = 0;
    size_t free_count = 0;
    size_t bytes_allocated = 0;
    size_t peak_allocated = 0;
    size_t current_allocated = 0;

    TestCustomAllocator(OrtAllocatorType alloc_type) {
        version = ORT_API_VERSION;
        Alloc = AllocStatic;
        Free = FreeStatic;
        Info = InfoStatic;
        Reserve = ReserveStatic;

        const auto& api = Ort::GetApi();
        auto status = api.CreateCpuMemoryInfo(alloc_type, OrtMemTypeDefault, &mem_info);
        if (status) {
            std::cerr << "Failed to create cpu memory info: " << api.GetErrorMessage(status) << std::endl;
            api.ReleaseStatus(status);
        }
    }

    ~TestCustomAllocator() {
        if (mem_info) {
            Ort::GetApi().ReleaseMemoryInfo(mem_info);
        }
    }

    static void* ORT_API_CALL AllocStatic(struct OrtAllocator* this_, size_t size) {
        auto* self = static_cast<TestCustomAllocator*>(this_);
        self->alloc_count++;
        self->bytes_allocated += size;
        self->current_allocated += size;
        if (self->current_allocated > self->peak_allocated) {
            self->peak_allocated = self->current_allocated;
        }
        size_t total = size + 16;
        void* raw = malloc(total);
        if (!raw) return nullptr;
        *reinterpret_cast<size_t*>(raw) = size;
        return static_cast<char*>(raw) + 16;
    }

    static void ORT_API_CALL FreeStatic(struct OrtAllocator* this_, void* p) {
        if (!p) return;
        auto* self = static_cast<TestCustomAllocator*>(this_);
        self->free_count++;
        void* raw = static_cast<char*>(p) - 16;
        size_t size = *reinterpret_cast<size_t*>(raw);
        if (self->current_allocated >= size) {
            self->current_allocated -= size;
        } else {
            self->current_allocated = 0;
        }
        free(raw);
    }

    static const struct OrtMemoryInfo* ORT_API_CALL InfoStatic(const struct OrtAllocator* this_) {
        const auto* self = static_cast<const TestCustomAllocator*>(this_);
        return self->mem_info;
    }

    static void* ORT_API_CALL ReserveStatic(struct OrtAllocator* this_, size_t size) {
        return AllocStatic(this_, size);
    }
};

int main() {
    std::string model_path = "first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx";
    const auto& api = Ort::GetApi();

    std::cout << "Creating Custom Allocator (OrtDeviceAllocator)..." << std::endl;
    TestCustomAllocator custom_alloc(OrtDeviceAllocator);

    OrtEnv* c_env = nullptr;
    auto status = api.CreateEnv(ORT_LOGGING_LEVEL_WARNING, "yolo_custom_env", &c_env);
    if (status) {
        std::cerr << "CreateEnv failed: " << api.GetErrorMessage(status) << std::endl;
        return 1;
    }

    status = api.RegisterAllocator(c_env, &custom_alloc);
    if (status) {
        std::cerr << "RegisterAllocator failed: " << api.GetErrorMessage(status) << std::endl;
        api.ReleaseStatus(status);
        api.ReleaseEnv(c_env);
        return 1;
    }

    Ort::Env env(c_env);
    Ort::SessionOptions session_options;
    session_options.SetIntraOpNumThreads(1);
    session_options.SetInterOpNumThreads(1);
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    
    // Explicitly enable env allocators in session!
    session_options.AddConfigEntry(kOrtSessionOptionsConfigUseEnvAllocators, "1");
    session_options.DisableCpuMemArena(); // Route allocations directly to custom device allocator!

    std::cout << "Loading YOLO26n session..." << std::endl;
    Ort::Session session(env, model_path.c_str(), session_options);

    std::cout << "After Model Load:" << std::endl;
    std::cout << "  Allocs: " << custom_alloc.alloc_count 
              << ", Frees: " << custom_alloc.free_count 
              << ", Bytes: " << custom_alloc.bytes_allocated / (1024.0 * 1024.0) << " MB"
              << ", Current Live: " << custom_alloc.current_allocated / (1024.0 * 1024.0) << " MB" << std::endl;

    Ort::AllocatorWithDefaultOptions default_allocator;
    auto input_name_ptr = session.GetInputNameAllocated(0, default_allocator);
    auto output_name_ptr = session.GetOutputNameAllocated(0, default_allocator);
    const char* input_names[] = { input_name_ptr.get() };
    const char* output_names[] = { output_name_ptr.get() };

    std::vector<int64_t> input_shape = {1, 3, 640, 640};
    size_t input_elements = 1 * 3 * 640 * 640;
    std::vector<float> input_tensor_values(input_elements, 0.5f);

    auto mem_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        mem_info, input_tensor_values.data(), input_elements,
        input_shape.data(), input_shape.size()
    );

    std::cout << "Running 5 inference passes..." << std::endl;
    for (int frame = 0; frame < 5; ++frame) {
        size_t before_allocs = custom_alloc.alloc_count;
        size_t before_frees = custom_alloc.free_count;
        auto output_tensors = session.Run(
            Ort::RunOptions{nullptr},
            input_names, &input_tensor, 1,
            output_names, 1
        );
        (void)output_tensors;
        std::cout << "  Frame " << frame << " -> New Allocs: " << (custom_alloc.alloc_count - before_allocs)
                  << ", New Frees: " << (custom_alloc.free_count - before_frees)
                  << ", Current Live: " << custom_alloc.current_allocated / (1024.0 * 1024.0) << " MB" << std::endl;
    }

    std::cout << "Final Total:" << std::endl;
    std::cout << "  Total Allocs: " << custom_alloc.alloc_count 
              << ", Total Frees: " << custom_alloc.free_count 
              << ", Peak Live: " << custom_alloc.peak_allocated / (1024.0 * 1024.0) << " MB"
              << ", End Live: " << custom_alloc.current_allocated / (1024.0 * 1024.0) << " MB" << std::endl;

    return 0;
}
