# TRT2-06 pinned memory 与同机对比

## 改动

- `Detector` 的输入/输出改为 `cudaHostAlloc` pinned host 缓冲；`cudaMemcpyAsync` H2D/D2H 不再对 pageable 内存做隐式同步。
- 新增 `Detector::input()`（pinned 输入，调用方直接写入）和 `Detector::infer()`（返回 pinned 输出指针），去掉每帧 1.2MB 输入 vector 和 50KB 输出 vector 分配。
- `image_detection`、`vision_detect`、`trt_infer` 同步适配；预处理、解码、NMS、跟踪逻辑未改。
- 单帧同步 API 下 H2D/推理/D2H 天然串行，本轮不启用多 stream；后续多帧流水线再评估 overlap。

## 构建

~~~powershell
$env:TensorRT_ROOT = 'F:\cf-build\TensorRT-8.6.1.6'
$env:Path = "F:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;F:\opencv\opencv\build\x64\vc16\bin;$env:TensorRT_ROOT\lib;$env:CUDA_PATH\bin;$env:Path"
cmake --preset windows-sdk -DVISION_WITH_OPENCV=ON -DOpenCV_DIR=F:/opencv/opencv/build
cmake --build --preset windows-sdk
ctest --preset windows-sdk
~~~

## 同机对比

机器：GTX 1060 6GB，驱动 582.78，CUDA 11.8.89，TensorRT 8.6.1.6，OpenCV 4.13.0。

~~~powershell
# before 二进制保存在 artifacts/bench/before，after 为当前构建
.\.venv\Scripts\python.exe tools\benchmark_replay.py artifacts\bench\before\vision_replay.exe artifacts\engines\yolo11n-v002-fp32\best-fp32.engine artifacts\replay\mot-v1\fixture.avi --out artifacts\bench\before-bench --tracker artifacts\bench\before\tracker.yml --config artifacts\bench\before\replay.yml --runs 3 --label before
.\.venv\Scripts\python.exe tools\benchmark_replay.py out\windows-sdk\apps\trt_infer\Release\vision_replay.exe artifacts\engines\yolo11n-v002-fp32\best-fp32.engine artifacts\replay\mot-v1\fixture.avi --out artifacts\bench\after-bench --tracker out\windows-sdk\apps\trt_infer\Release\tracker.yml --config out\windows-sdk\apps\trt_infer\Release\replay.yml --runs 3 --label after
~~~

两轮各 3 次、blocking 1000 帧，取跨轮中位数：

| 指标 | P50 before | P50 after | Δ | P95 before | P95 after | Δ | P99 before | P99 after | Δ |
|---|---|---|---|---|---|---|---|---|---|
| queue_wait_ms | 10.2753 | 9.3196 | -9.3% | 13.4535 | 12.3842 | -7.9% | 16.1334 | 15.3124 | -5.1% |
| infer_ms | 3.9750 | 3.5321 | -11.1% | 5.9055 | 5.7574 | -2.5% | 7.2063 | 6.6838 | -7.3% |
| mot_ms | 0.0108 | 0.0100 | -7.4% | 0.0157 | 0.0149 | -5.1% | 0.0201 | 0.0230 | +14.4% |
| result_age_ms | 14.3422 | 12.8994 | -10.1% | 19.1861 | 17.2873 | -9.9% | 22.2006 | 19.4520 | -12.4% |

`mot_ms` 在 0.01-0.02ms 量级，P99 的 +14.4% 属于噪声；主要收益在 infer 和 result_age P50/P95/P99。

## 正确性

- `vision_detect` 同图前后输出 2 个框，字段完全一致。
- `trt_infer` 同一 1×3×320×320 输入，原始输出 50400 字节逐字节一致。
- before/after 各 3 轮 blocking 1000 帧，frames.jsonl 稳定字段都与 `artifacts/replay/mot-v1/first/frames.jsonl` 逐字段一致。
- `tools/verify_replay.py` 两轮哈希 `25c13759…` 一致；`verify_trt.py` 三张实图 max_abs 与 image-check 不变。
- `windows-cpu` 21/21、`windows-full` 21/21、`windows-sdk` 3/3、pytest 10/10（SIM-01 接入后 CTest 由 20 变 21）。

## 证据

`artifacts/bench/before-bench/benchmark.json`、`after-bench/benchmark.json`、`comparison.json`、`before/`、`after/`（artifacts 被 Git 忽略）。
