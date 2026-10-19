# CV Model Storage & Path Specification

### 1. Active Computer Vision Model
The active computer vision model for all benchmark evaluations in this project is **Ultralytics YOLO26n** (January 2026 SOTA):

* **Exact Host Path**:
  [`/Users/kenza/StudyProjects/SJTU/alloc/first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx`](file:///Users/kenza/StudyProjects/SJTU/alloc/first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx)
* **Relative Path from Project Root**:
  `first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx`
* **Docker Container Path**:
  `/lab/first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx`
* **File Size**: 9,941,957 bytes (9.48 MB)
* **SHA-256 Checksum**: `2e947b787d9e787b93a16772a5f55b1d4d8c4d86f53146149c5d6a642442d6f7`
* **Format**: ONNX
* **Input Tensor**: `[1, 3, 640, 640]` (float32)
* **Output Tensor**: `[1, 300, 6]` (float32, NMS-free end-to-end detections)

---

### 2. Consumer Benchmark Runners
All benchmarks locate and open this model using the relative path `first_steps/bench/yolo26n_pipeline/models/yolo26n.onnx`:
* Single-stream baseline: [first_steps/bench/yolo26n_pipeline/yolo26_bench.cpp](file:///Users/kenza/StudyProjects/SJTU/alloc/first_steps/bench/yolo26n_pipeline/yolo26_bench.cpp)
* Multi-camera scaling: [first_steps/bench/multi_camera_yolo26/multi_camera_bench.cpp](file:///Users/kenza/StudyProjects/SJTU/alloc/first_steps/bench/multi_camera_yolo26/multi_camera_bench.cpp)
* Adversarial fragmentation suite: [first_steps/bench/adversarial_yolo26_bench/adversarial_runner.cpp](file:///Users/kenza/StudyProjects/SJTU/alloc/first_steps/bench/adversarial_yolo26_bench/adversarial_runner.cpp)
* Multi-camera adversarial suite: [first_steps/bench/multi_camera_adversarial/multi_camera_adversarial_bench.cpp](file:///Users/kenza/StudyProjects/SJTU/alloc/first_steps/bench/multi_camera_adversarial/multi_camera_adversarial_bench.cpp)
* Native ORT Custom Arena: [first_steps/bench/ort_custom_arena/ort_custom_arena_bench.cpp](file:///Users/kenza/StudyProjects/SJTU/alloc/first_steps/bench/ort_custom_arena/ort_custom_arena_bench.cpp)
* 5-trial statistical suite: [first_steps/bench/precise_yolo26_bench/precise_yolo26_bench.cpp](file:///Users/kenza/StudyProjects/SJTU/alloc/first_steps/bench/precise_yolo26_bench/precise_yolo26_bench.cpp)
