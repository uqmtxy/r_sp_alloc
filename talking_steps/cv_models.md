# Relevant Open-Source Computer Vision Models for Edge Systems Memory Research

## 1. Overview & Evaluation Scope
In dedicated factory edge servers (Automated Optical Inspection / Quality Control), memory bottlenecks are not caused by model parameters alone, but by **systems-level memory interactions**:
1. **Activation Churn**: Rapid allocation/deallocation of dozens of intermediate feature maps per frame (at 30–120 FPS).
2. **Internal Size Slack**: Non-power-of-two tensor dimensions (e.g., $6.25\,\text{MB}$) rounded up by generic slab allocators to $8\,\text{MB}$ ($28\%\text{--}40\%$ waste).
3. **Lifetime Intermixing (Wilson 1995)**: Short-lived $16\,\text{ms}$ frame activations sharing physical pages with long-lived model weights or feature memory banks, permanently pinning physical DRAM.
4. **Thread-Cache Hoarding**: Camera ingestion threads handing buffers to inference threads, causing `jemalloc`/`TCMalloc` thread-local caches to hoard empty memory.

---

## 2. Top 5 Open-Source CV Models & Exposed Memory Bottlenecks

| Model / Framework | Architecture & Task | Primary Systems Memory Bottleneck Exposed | Open-Source Link |
| :--- | :--- | :--- | :--- |
| 
| **Intel Anomalib: PatchCore** | Pretrained Backbone + Coreset Memory Bank (Unsupervised Anomaly Detection) | Massive static memory bank ($200\,\text{MB}\text{--}1.5\,\text{GB}$) in DRAM, permanent page pinning, and large pairwise distance matrix allocation storms. | [GitHub: openvinotoolkit/anomalib](https://github.com/openvinotoolkit/anomalib) / [CVPR '22 Paper](https://openaccess.thecvf.org/content/CVPR2022/html/Roth_Towards_Total_Recall_in_Industrial_Anomaly_Detection_CVPR_2022_paper.html) |
| **Baidu RT-DETRv2** | Hybrid CNN + Deformable Attention Vision Transformer (High-Precision Defect Detection) | Multi-head self-attention scratchpads ($QK^T$) creating large burst spikes in Peak RSS (`VmHWM`) and hardware TLB thrashing. | [GitHub: lyuwenyu/RT-DETR](https://github.com/lyuwenyu/RT-DETR) / [CVPR '24 Paper](https://openaccess.thecvf.org/content/CVPR2024/html/Zhao_DETRs_Beat_YOLOs_on_Real-Time_Object_Detection_CVPR_2024_paper.html) |
| **ResNet-UNet / SegFormer** | Encoder-Decoder with Skip Connections (Pixel-Level Flaw Segmentation) | Skip connections force early high-resolution encoder feature maps to remain alive across downsampling, multiplying the concurrent working set. | [GitHub: qubvel/segmentation_models.pytorch](https://github.com/qubvel/segmentation_models.pytorch) |
| **MobileSAM / FastSAM** | Decoupled TinyViT + Prompt Mask Decoder (Interactive Defect Boundary Inspection) | High-resolution token embedding matrices ($1024\times 1024$) and variable polygon output sizing stressing heap fragmentation. | [GitHub: ChaoningZhang/MobileSAM](https://github.com/ChaoningZhang/MobileSAM) |

---

## 3. Recommended Priority Targets for Empirical Benchmarks

For maximum research impact and contrasting systems behaviors, prioritize these two models:

1. **Target A: Ultralytics YOLO11 / YOLOv8 (Real-Time Workhorse)**
   - **Why**: Represents $>80\%$ of factory edge deployments.
   - **Systems Win to Prove**: Exact-fit slabs and circular pre-allocated frame ring arenas eliminate activation churn, internal size padding, and minor page faults at 60–120 FPS.
   - **Export Format**: Standard ONNX model executed via C++ ONNX Runtime.

2. **Target B: Intel Anomalib PatchCore (Industrial Anomaly Detection)**
   - **Why**: Industry gold standard for zero-shot defect detection on the MVTec AD benchmark.
   - **Systems Win to Prove**: Lifetime segregation (separating the static coreset memory bank from per-frame query patches) and out-of-band purging (`process_madvise`) prevent page pinning and memory bloat.
   - **Export Format**: ONNX/OpenVINO representation with C++ inference runner.
