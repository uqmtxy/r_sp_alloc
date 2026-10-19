# Ultralytics Model Choice: YOLO26 (2026 SOTA) vs. YOLOv8 (Classic)

### The 2026 Reality: Ultralytics YOLO26
The current flagship Ultralytics model is **YOLO26** (released January 2026, [arXiv:2606.03748](https://arxiv.org/abs/2606.03748)).

### Head-to-Head Comparison for Memory Research

| Feature | **`yolo26n`** (2026 SOTA) | **`yolov8n`** (Classic Workhorse) |
| :--- | :--- | :--- |
| **Release Date** | **January 2026** | January 2023 |
| **Inference Architecture** | **Native NMS-Free End-to-End (`nms=False`)** | Standard anchor-free with dynamic NMS |
| **Post-Processing Churn** | **Zero dynamic heap churn** (no NMS vector allocations) | Significant dynamic churn (NMS sorting/filtering) |
| **DAG Predictability** | **100% deterministic static shapes from input to output** | Static backbone, but variable output buffer sizes |
| **Relevance to PLOS '26 & ISMM '26** | **Ideal**: A pure static DAG is the exact workload where page-grid alignment and circular frame ring arenas achieve maximum memory savings. | **Realistic legacy**: Shows how allocators struggle when static CNN buffers mix with dynamic post-processing vectors. |
| **CPU ONNX Efficiency** | Up to 43% faster ONNX CPU inference vs older generations | Baseline CPU ONNX performance |

---

### Direct Recommendation

* **`yolo26n` (Recommended)**: It is the current 2026 SOTA. Its **NMS-free end-to-end design** turns the vision pipeline into a 100% predictable, static DAG—making it the ideal testbed for page-grid memory management and Wilson lifetime isolation.
* **`yolov8n`**: Pick this only if you need backward comparability with pre-2024 academic papers.
