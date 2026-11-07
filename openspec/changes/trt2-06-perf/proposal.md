# TRT2-06 GPU 性能优化

## 目标

在不改变检测结果和跟踪轨迹的前提下，降低单帧推理链路的 CPU/GPU 传输开销：

- Detector 使用 pinned host 输入/输出缓冲；
- 去掉 image_detection 每帧 1.2MB 输入 vector 分配和 50KB 输出 vector 分配；
- 用同一机器、同一 Engine、同一夹具做前后 benchmark；
- 多 stream 只在实际有收益时启用，否则记录不启用理由。

## 范围

- `apps/trt_infer/detector.{hpp,cpp}`：pinned buffer、输入/输出缓冲所有权、异步拷贝。
- `apps/trt_infer/image_detection.hpp`、`image_main.cpp`、`main.cpp`：适配新 API，保持输出契约。
- `tools/benchmark_replay.py`：重复运行并汇总 P50/P95/P99，生成 JSON。
- 不修改 Engine、模型、跟踪算法、阈值。

## 非目标

- 不改 FP16/INT8、不改 batch、不改输入尺寸。
- 不在本轮引入多 GPU 或 CUDA Graph。
- 不承诺固定 FPS 数字；只报告同机前后对比。
