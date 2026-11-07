# 设计

## Detector 缓冲

`Detector::State` 增加两块 `cudaHostAlloc` pinned 内存：

- 输入 `3*320*320` float，调用方通过 `Detector::input()` 直接写入；
- 输出 `6*2100` float，`Detector::infer()` 返回其只读指针。

设备侧继续使用原有 `cudaMalloc` 输入/输出和一条 `cudaStream`。`infer()` 的顺序：
`cudaMemcpyAsync(H2D)` → `enqueueV3` → `cudaMemcpyAsync(D2H)` → `cudaStreamSynchronize`。
pinned 内存使两次 async copy 真正异步，避免 pageable 内存的隐式同步。

## API 与生命周期

- `float* Detector::input()`：pinned 输入缓冲，调用方在 `infer()` 前写满 `3*320*320` 个 float。
- `const float* Detector::infer()`：运行并返回 pinned 输出；指针在下一次 `infer()` 前有效。
- 删除返回 `std::vector<float>` 的旧接口；两个调用点（`image_detection.hpp`、`main.cpp`）同步修改。
- `State` 析构释放 host pinned 和 device 缓冲；异常路径不泄漏。
- 单线程使用假设不变：一个 Detector 只被一个消费者线程调用。

## image_detection

`detect()` 不再创建 `std::vector<float> input`，直接把三个通道写入 `detector.input()` 的 planes；读取 `detector.infer()` 返回的裸指针。预处理、解码、NMS、坐标还原逻辑不变。

## benchmark

`tools/benchmark_replay.py` 对同一 exe/engine/video/config 重复 N 次，每次新输出目录，读取 summary.json 的 `infer_ms`、`queue_wait_ms`、`mot_ms`、`result_age_ms`，取跨轮 P50/P95/P99 的中位数，输出 benchmark.json 和表格。

前后对比使用 `artifacts/bench/before/` 中保存的旧二进制与构建后的新二进制；同一 GPU、驱动、Engine、夹具、blocking 配置。比较 `infer_ms`/`result_age_ms`，同时核对 `verify_replay` 的稳定字段和 `vision_detect` 的框输出不变。

## 多 stream 取舍

单帧同步 API 下 H2D/推理/D2H 天然串行，多 stream 只会增加事件同步开销；本轮不启用。若后续引入多帧流水线，再评估两个 stream 的 overlap。

## 回退

恢复 `Detector` 旧接口与 pageable 拷贝即可；保留 before 二进制和 benchmark 结果作为回退基线。
