# 动态目标视觉平台（阶段 0–1 最小闭环）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 交付可独立运行的闭环：视频回放 → YOLO → ONNX → TensorRT C++ → 检测可视化和 JSONL，并在处理视频流的同时实时筛选最佳照片。不含跟踪和预测算法，它们属于阶段 2 的新计划。

**Architecture:** `vision_core` 是纯 C++17 库，可在 macOS 上构建和测试；`vision_trt` 只在 Windows 构建。`replay_runner` 通过 `IDetector` 选择假检测器或 TensorRT 检测器，所以 macOS 能验证除 GPU 外的整条链路。

**Tech Stack:** C++17、CMake ≥3.26、GoogleTest、yaml-cpp、nlohmann_json、PicoSHA2、OpenCV 4（可选）、Python 3.12 + uv + Ultralytics、NVIDIA TensorRT Windows SDK。

**Spec:** `/Users/mataiming/project/tensor-rt/TensorRT-11.4/docs/superpowers/specs/2026-11-06-dynamic-target-vision-platform-design.md`

## Global Constraints

- 工程目录是 `/Users/mataiming/project/tensor-rt/dynamic-vision/`，不修改 `TensorRT-11.4/` 源码。
- `Types.hpp` 是契约唯一定义处，其他文件不得重复定义这些类型。
- 所有对外坐标是原图像素，左上角为原点。
- `TimestampNs` 是会话内单调纳秒，不是 Unix 时间。
- `IFrameSource` 只有 `VideoFileSource`、`SyntheticSource`、`HikCameraSource` 三种实现；本计划不实现 `HikCameraSource`。
- 不存在向外发送控制量的接口，输出只写文件或本地显示。
- 所有可调参数放在 YAML，改配置不重新编译；超范围启动即报错，不静默修正。
- 每次运行把生效配置和 SHA-256 写入输出目录。
- 不承诺精度或延迟数字，所有阈值先测基线。
- 报告和文档使用中文。
- 不导入 `legacy_center320` 或无来源记录的数据。

## Review Focus

1. 空视频、损坏帧或全程无检测：不崩溃。空视频报错退出；无检测帧仍写一行 JSONL。（Task 6、9）
2. 模型清单与 ONNX 或引擎输出形状不符：启动即报错，不输出错误框。（Task 5、8）
3. 引擎与 ONNX 或工具链不匹配：拒绝加载。（Task 8）
4. 非 640 比例的画面、奇数尺寸、靠边的框：逆变换后裁到画面内，零面积框被丢弃。（Task 3、5）
5. 视频缺少时间戳旁路文件：按帧率推算，并标记 `timestamp_estimated=true`。（Task 9）

---

## 文件结构

```text
dynamic-vision/
  CMakeLists.txt, cmake/Dependencies.cmake, README.md
  docs/design.md                    # 设计文档副本
  docs/tuning.md                    # 中文调参说明
  docs/adr/0001-toolchain.md
  openspec/project.md
  openspec/changes/phase-0-1-minimal-loop/{proposal,design,tasks}.md
  openspec/changes/phase-0-1-minimal-loop/specs/replay-pipeline/spec.md
  config/default.yaml
  include/vision/contracts/{Types,Interfaces,Result}.hpp
  include/vision/core/{Config,Letterbox,BoundedQueue,YoloDecoder,ModelManifest,Stats,Pipeline}.hpp
  include/vision/io/{JsonlSink,SyntheticSource,FakeDetector,VideoFileSource,VideoOverlaySink}.hpp
  include/vision/trt/{EngineManifest,TensorRtDetector}.hpp
  src/core/, src/io/, src/trt/
  apps/{env_probe,engine_builder,replay_runner,eval_report}/main.cpp
  tests/unit/, tests/replay/, tests/golden/
  tools/check_boundary.py
  python/{pyproject.toml,train.py,export_onnx.py,make_golden.py}
  scripts/win/{phase0_probe.ps1,phase1_run.ps1}
```

---

### Task 1: 工程骨架、OpenSpec 与边界扫描

**Files:**
- Create: `CMakeLists.txt`、`cmake/Dependencies.cmake`、`README.md`、`.gitignore`、`docs/design.md`、`openspec/**`、`tools/check_boundary.py`、`tests/unit/test_smoke.cpp`、`tests/tools/test_check_boundary.py`

**Interfaces:**
- Produces: CMake 目标 `vision_core`（STATIC）、`vision_tests`；选项 `VISION_WITH_OPENCV`（默认 OFF）、`VISION_WITH_TRT`（默认 OFF，且仅 WIN32 允许 ON）；`check_boundary.py <root>` 退出码 0 表示干净，1 表示违规并打印 `文件:行:符号`。

- [ ] **Step 1: 初始化**

```bash
mkdir -p /Users/mataiming/project/tensor-rt/dynamic-vision && cd $_ && git init
cp ../TensorRT-11.4/docs/superpowers/specs/2026-11-06-dynamic-target-vision-platform-design.md docs/design.md
```

- [ ] **Step 2: 写 OpenSpec 变更**

手写 `openspec/` 目录，内容与本计划和设计第 9 节阶段 0–1 一致。`spec.md` 用 `### Requirement:` / `#### Scenario:` 格式覆盖 Review Focus 五项。

Run: `npx -y @fission-ai/openspec@latest validate phase-0-1-minimal-loop`
Expected: 校验通过。若网络不可用，在 README 记录未校验，不阻塞后续任务。

- [ ] **Step 3: 写失败测试**

`tests/tools/test_check_boundary.py`：临时目录放入含 `SendInput(` 的 `.cpp`，断言退出码 1 且输出含文件名；放入干净文件，断言退出码 0。

Run: `uv run --with pytest pytest tests/tools -q`
Expected: FAIL（脚本不存在）

- [ ] **Step 4: 实现 `check_boundary.py`**

扫描 `.cpp/.hpp/.py/.ps1/.cmake`，跳过 `build/`、`tests/tools/`。禁用符号：`SendInput`、`mouse_event`、`keybd_event`、`SetWindowsHookEx`、`IDXGIOutputDuplication`、`DuplicateOutput`、`BitBlt`、`WriteProcessMemory`、`CreateRemoteThread`、`pyautogui`、`pynput`。

- [ ] **Step 5: 搭 CMake 和 smoke 测试**

用 FetchContent 拉取 GoogleTest、yaml-cpp、nlohmann_json、PicoSHA2，固定到具体 tag。

Run: `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure && uv run --with pytest pytest tests/tools -q && python3 tools/check_boundary.py .`
Expected: 全部通过，退出码 0

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "chore: scaffold dynamic-vision with OpenSpec and boundary scan"
```

### Task 2: 数据契约与配置加载

**Files:**
- Create: `include/vision/contracts/{Types,Interfaces,Result}.hpp`、`include/vision/core/Config.hpp`、`src/core/Config.cpp`、`config/default.yaml`、`docs/tuning.md`
- Test: `tests/unit/test_types.cpp`、`tests/unit/test_config.cpp`

**Interfaces:**
- Produces:
  - `Types.hpp`：设计第 5 节全部类型（含 `FocusState` 和 `FrameMeta::focus_state`），外加 `struct FrameResult { FrameMeta meta; std::vector<Detection> detections; std::vector<TrackSnapshot> tracks; std::vector<Prediction> predictions; StageTimings timings; uint64_t dropped_frames{}, stale_discards{}; bool timestamp_estimated{}; };`
  - `Result.hpp`：`template<class T> class Result`，提供 `ok()`、`value()`、`error()`、静态 `Ok(T)`、`Err(std::string)`。
  - `Interfaces.hpp`：`enum class PixelFormat { Bgr8, Rgb8, BayerRg8 }`、`enum class MemoryKind { Host, Pinned, Device }`、`struct ImageView { const void* data; int width, height, step_bytes; PixelFormat format; MemoryKind memory; }`、`struct Frame { FrameMeta meta; ImageView image; std::shared_ptr<void> owner; bool timestamp_estimated{}; }`；`IFrameSource`、`IDetector`、`IResultSink`。`ITracker`、`IPredictor` 仅声明，阶段 1 不实现。
  - `Config.hpp`：`Result<AppConfig> loadConfig(const std::filesystem::path&)`；`std::string effectiveYaml(const AppConfig&)`；`std::string configSha256(const AppConfig&)`。

- [ ] **Step 1: 写失败测试**

```cpp
TEST(Config, RejectsOutOfRangeConfidence) {
  auto r = loadConfig(fixture("bad_confidence.yaml")); // detector.conf_threshold: 1.5
  ASSERT_FALSE(r.ok());
  EXPECT_NE(r.error().find("detector.conf_threshold"), std::string::npos);
}
TEST(Config, RejectsUnknownKey) { /* detecter: 拼写错误 → 报错并指出键名 */ }
TEST(Config, HashIsStable) { /* 同一配置两次加载，sha 相同；改一个值，sha 不同 */ }
TEST(Types, DefaultsAreZero) { /* FrameMeta{}.model_epoch == 0; TrackSnapshot{}.class_id == 0 */ }
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build -j && ctest --test-dir build -R 'Config|Types' --output-on-failure`
Expected: 编译失败（头文件不存在）

- [ ] **Step 3: 实现**

`AppConfig` 含 `input{imgsz=640}`、`detector{conf_threshold=0.25, nms_iou=0.45, max_detections=100}`、`queue{capacity=2, mode}`（`latest` 或 `blocking`）、`output{dir, write_video}`、`prediction{horizons_ms=[10,30,100]}`。未知键报错。

`docs/tuning.md` 按设计第 13 节写清楚每个参数。默认值注明“未经验证”。

- [ ] **Step 4: 运行确认通过**

Run: `ctest --test-dir build -R 'Config|Types' --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add data contracts and validated config"
```

### Task 3: 坐标变换

**Files:**
- Create: `include/vision/core/Letterbox.hpp`、`src/core/Letterbox.cpp`
- Test: `tests/unit/test_letterbox.cpp`

**Interfaces:**
- Consumes: `RectF`、`PointF`
- Produces: `struct LetterboxMap { float scale, pad_x, pad_y; int src_w, src_h, dst_w, dst_h; }`；`LetterboxMap makeLetterbox(int src_w, int src_h, int dst_w, int dst_h)`；`PointF toNetwork(const LetterboxMap&, PointF)`；`PointF toSource(const LetterboxMap&, PointF)`；`std::optional<RectF> rectToSource(const LetterboxMap&, RectF net_xywh)`（裁到原图内，宽或高 ≤0 时返回 nullopt）。

- [ ] **Step 1: 写失败测试**

```cpp
TEST(Letterbox, CameraResolutionRoundTrip) {
  auto m = makeLetterbox(1440, 1080, 640, 640);
  EXPECT_FLOAT_EQ(m.scale, 640.f / 1440.f);
  EXPECT_FLOAT_EQ(m.pad_x, 0.f);
  EXPECT_FLOAT_EQ(m.pad_y, 80.f);
  auto p = toSource(m, toNetwork(m, {123.25f, 456.5f}));
  EXPECT_NEAR(p.x, 123.25f, 1e-3); EXPECT_NEAR(p.y, 456.5f, 1e-3);
}
TEST(Letterbox, OddSizeRoundTrip) { /* 1279×721 → 640×640，往返误差 < 1e-2 */ }
TEST(Letterbox, BoxClippedToSource) { /* 超出右边界的框，x+w == 1440 */ }
TEST(Letterbox, BoxInPaddingIsDropped) { /* 完全落在上方填充区 → nullopt */ }
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build -j && ctest --test-dir build -R Letterbox --output-on-failure`
Expected: 编译失败

- [ ] **Step 3: 实现**

`scale=min(dst_w/src_w, dst_h/src_h)`；`pad=(dst-round(src*scale))/2`，并与 Python 侧预处理使用同一公式。

- [ ] **Step 4: 运行确认通过**

Run: `ctest --test-dir build -R Letterbox --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add reversible letterbox mapping"
```

### Task 4: 有界队列与耗时统计

**Files:**
- Create: `include/vision/core/{BoundedQueue,Stats}.hpp`、`src/core/Stats.cpp`
- Test: `tests/unit/test_queue.cpp`、`tests/unit/test_stats.cpp`

**Interfaces:**
- Produces: `template<class T> class BoundedQueue`，构造 `BoundedQueue(size_t cap, QueueMode mode)`，`enum class QueueMode { Latest, Blocking }`；`void push(T)`、`std::optional<T> pop()`（关闭且空时返回 nullopt）、`void close()`、`uint64_t dropped() const`。`struct LatencySummary { double p50, p95, p99, max; size_t n; }`；`LatencySummary summarize(std::vector<int64_t> ns)`。

- [ ] **Step 1: 写失败测试**

```cpp
TEST(Queue, LatestDropsOldest) { /* cap=2，push 1,2,3 → pop 得 2,3，dropped()==1 */ }
TEST(Queue, BlockingNeverDrops) { /* 生产者线程 push 1000 个，消费全部，顺序一致，dropped()==0 */ }
TEST(Queue, CloseWakesConsumer) { /* 阻塞中的 pop 在 close 后返回 nullopt */ }
TEST(Stats, Percentiles) { /* 1..100 → p50=50, p95=95, p99=99, max=100（最近秩法）*/ }
TEST(Stats, EmptyIsZero) { /* n==0 */ }
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build -j && ctest --test-dir build -R 'Queue|Stats' --output-on-failure`
Expected: 编译失败

- [ ] **Step 3: 实现**

mutex + 两个 condition_variable；分位数用最近秩法。

- [ ] **Step 4: 运行确认通过（含 TSan）**

Run: `cmake -S . -B build-tsan -DCMAKE_CXX_FLAGS=-fsanitize=thread && cmake --build build-tsan -j && ctest --test-dir build-tsan -R Queue --output-on-failure`
Expected: PASS，无数据竞争报告

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add bounded queue and latency stats"
```

### Task 5: 模型清单与 YOLO 解码

**Files:**
- Create: `include/vision/core/{ModelManifest,YoloDecoder}.hpp`、`src/core/{ModelManifest,YoloDecoder}.cpp`
- Test: `tests/unit/test_manifest.cpp`、`tests/unit/test_decoder.cpp`

**Interfaces:**
- Consumes: `LetterboxMap`、`rectToSource`、`Result`、`Detection`
- Produces: `struct ModelManifest { std::string model_id, onnx_sha256, input_name, output_name, layout, dtype, output_decoder; std::vector<int> input_shape, output_shape; std::vector<std::string> classes; float norm_scale; bool nms_embedded; }`；`Result<ModelManifest> loadManifest(path)`；`Result<void> validateOutputShape(const ModelManifest&, const std::vector<int>& actual)`；`std::vector<Detection> decodeYolo(const float* data, const std::vector<int>& shape, const ModelManifest&, const LetterboxMap&, float conf, float iou, int max_det)`。

`output_decoder` 第一版只支持 `ultralytics_xywh_cls`：形状 `[1, 4+nc, N]`，框为网络像素的中心点 xywh，类别分数已过 sigmoid，无内嵌 NMS。其他取值启动报错。此格式必须由 Task 7 的黄金样本实测确认，不能只凭本计划。

- [ ] **Step 1: 写失败测试**

```cpp
TEST(Manifest, RejectsUnknownDecoder) { /* output_decoder: foo → Err */ }
TEST(Manifest, RejectsNmsEmbedded) { /* nms_embedded: true → Err，第一版不支持 */ }
TEST(Manifest, ShapeMismatchFails) { /* classes=2，实际 [1,85,400] → Err，含两种形状 */ }
TEST(Decoder, FiltersByConfidence) { /* 两框分数 0.9/0.1，conf=0.25 → 1 个 */ }
TEST(Decoder, ClassAwareNms) { /* 同类重叠只留高分；异类重叠都保留 */ }
TEST(Decoder, MapsToSourcePixels) { /* 1440×1080，网络中心(320,320) w=64 h=64 → 原图框 x=648, y=468, w=144, h=144 */ }
TEST(Decoder, ZeroAreaDropped) { /* w=0 → 不输出 */ }
TEST(Decoder, HonorsMaxDetections) { /* 200 框，max_det=100 → 100 个，按分数降序 */ }
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build -j && ctest --test-dir build -R 'Manifest|Decoder' --output-on-failure`
Expected: 编译失败

- [ ] **Step 3: 实现**

NMS 是确定性的贪心算法：按分数降序，分数相同按输入下标升序。

- [ ] **Step 4: 运行确认通过**

Run: `ctest --test-dir build -R 'Manifest|Decoder' --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add model manifest and YOLO decoder"
```

### Task 6: 回放流水线（macOS 闭环）

**Files:**
- Create: `include/vision/core/Pipeline.hpp`、`src/core/Pipeline.cpp`、`include/vision/io/{SyntheticSource,FakeDetector,JsonlSink}.hpp`、`src/io/{SyntheticSource,FakeDetector,JsonlSink}.cpp`、`apps/replay_runner/main.cpp`
- Test: `tests/replay/test_pipeline.cpp`

**Interfaces:**
- Consumes: Task 2–5 全部
- Produces: `struct RunSummary { uint64_t frames, dropped; LatencySummary total; }`；`Result<RunSummary> runPipeline(IFrameSource&, IDetector&, IResultSink&, const AppConfig&)`。`SyntheticSource(SyntheticSpec)`：`SyntheticSpec { int width, height, frames, seed; double fps; int targets; }`，生成确定性的移动矩形和真值 `const std::vector<RectF>& truth(frame_id)`。`FakeDetector`：直接返回带固定噪声的真值。CLI：`replay_runner --config <yaml> --source synthetic|video --input <path> --detector fake|trt --output <dir>`。

输出目录包含：`results.jsonl`、`effective_config.yaml`、`config.sha256`、`summary.json`。JSONL 每行一个 `FrameResult`，第一行前不写表头。阶段 1 的 `tracks`、`predictions` 为空数组。

- [ ] **Step 1: 写失败测试**

```cpp
TEST(Pipeline, WritesOneLinePerFrame) { /* 100 帧，blocking → 100 行，frame_id 递增 */ }
TEST(Pipeline, DeterministicInBlockingMode) { /* 两次运行，去掉耗时字段后 JSONL 逐字节相同 */ }
TEST(Pipeline, EmptyDetectionsStillWritten) { /* targets=0 → 100 行，detections 为空 */ }
TEST(Pipeline, EmptySourceFails) { /* frames=0 → Err，含“无可读帧” */ }
TEST(Pipeline, UsesCaptureTimeNotCompletionTime) { /* capture_ns == frame_id*1e9/fps */ }
TEST(Pipeline, DetectorErrorStopsRun) { /* 检测器返回 Err → runPipeline 返回 Err，已写结果保留 */ }
TEST(Pipeline, TimingsPopulated) { /* total_ns > 0 */ }
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build -j && ctest --test-dir build -R Pipeline --output-on-failure`
Expected: 编译失败

- [ ] **Step 3: 实现**

三线程：采集 → `BoundedQueue<Frame>` → 推理 → `BoundedQueue<FrameResult>` → 输出。任一环节出错时关闭队列并等待线程退出。

- [ ] **Step 4: 运行确认通过，并跑 CLI**

Run: `ctest --test-dir build -R Pipeline --output-on-failure && ./build/apps/replay_runner/replay_runner --config config/default.yaml --source synthetic --detector fake --output out/smoke && wc -l out/smoke/results.jsonl`
Expected: PASS；行数等于合成帧数；`config.sha256` 存在

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add replay pipeline with synthetic source and JSONL output"
```

### Task 7: 训练、导出与黄金样本

**Files:**
- Create: `python/pyproject.toml`、`python/{train.py,export_onnx.py,make_golden.py}`、`python/tests/test_manifest_export.py`、`tests/unit/test_golden_decode.cpp`

**Interfaces:**
- Consumes: Task 5 的清单字段
- Produces: `export_onnx.py --weights best.pt --imgsz 640 --out artifacts/<model_id>/` 输出 `model.onnx`、`model_manifest.yaml`；`make_golden.py` 输出 `tests/golden/<model_id>/{input.bin,output.bin,expected.json}`，其中 `input.bin` 是预处理后的 float32 张量，`output.bin` 是 ONNX 原始输出，`expected.json` 是 PyTorch 后处理结果。

Ultralytics 导出固定 `nms=False`，版本锁定在 `pyproject.toml`。`export_onnx.py` 用 `onnx` 包读取实际输入输出形状写入清单，不按模型名猜测。

- [ ] **Step 1: 写失败测试**

`test_manifest_export.py`：用一个随机权重小模型导出，断言清单的 `output_shape[1] == 4 + len(classes)`，`onnx_sha256` 与文件哈希一致，`nms_embedded` 为 false。

Run: `cd python && uv run pytest tests -q`
Expected: FAIL（脚本不存在）

- [ ] **Step 2: 实现三个脚本**

- [ ] **Step 3: 运行确认通过**

Run: `cd python && uv run pytest tests -q`
Expected: PASS

- [ ] **Step 4: 写 C++ 黄金测试**

`test_golden_decode.cpp`：读取 `output.bin`，用 `decodeYolo` 解码，与 `expected.json` 比较：数量相同，类别相同，框坐标差 ≤1 px，分数差 ≤1e-3。比较阈值只作一致性检查。若样本目录不存在则 `GTEST_SKIP()`。

Run: `cmake --build build -j && ctest --test-dir build -R Golden --output-on-failure`
Expected: PASS。若失败，说明 Task 5 的解码格式与实际导出不符，必须修解码器或清单，不能放宽阈值。

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add training, ONNX export and golden samples"
```

### Task 8: TensorRT 环境探针、引擎构建与检测器（Windows）

macOS 无法编译这部分。实现者只能做到代码完成和 macOS 下的引擎清单单测，**不得报告 Windows 部分已验证**，必须交给用户在 Windows 上运行。

**Files:**
- Create: `apps/env_probe/main.cpp`、`apps/engine_builder/main.cpp`、`include/vision/trt/{EngineManifest,TensorRtDetector}.hpp`、`src/trt/{EngineManifest,TensorRtDetector}.cpp`、`cmake/FindTensorRTSdk.cmake`、`scripts/win/phase0_probe.ps1`、`docs/adr/0001-toolchain.md`
- Test: `tests/unit/test_engine_manifest.cpp`

**Interfaces:**
- Consumes: `IDetector`、`ModelManifest`、`validateOutputShape`、`decodeYolo`、`LetterboxMap`
- Produces: `struct EngineManifest { std::string engine_sha256, onnx_sha256, trt_version, cuda_version, gpu_name, precision; }`；`Result<void> checkEngineCompatible(const EngineManifest&, const std::string& onnx_sha256, const EngineManifest& current_env)`（纯 C++，放在 `vision_core`，可在 macOS 测试）；`Result<std::unique_ptr<TensorRtDetector>> TensorRtDetector::create(engine_path, ModelManifest, const AppConfig&)`。

TensorRT 调用按 `TensorRT-11.4/samples/sampleOnnxMNIST/sampleOnnxMNIST.cpp` 和 `include/NvInferRuntime.h`：`createInferBuilder`、`nvonnxparser::createParser`、`parseFromFile`、`createBuildConfig`、`buildSerializedNetwork`、`createInferRuntime`、`deserializeCudaEngine`、`createExecutionContext`、`setTensorAddress`、`enqueueV3`。首版用同步 `cudaStreamSynchronize`，异步优化留到阶段 3。

`FindTensorRTSdk.cmake` 用 `TENSORRT_ROOT` 查找 `NvInfer.h`，用 `file(GLOB)` 查找 `nvinfer*.lib` 和 `nvinfer_plugin*.lib`，不写死库名。

- [ ] **Step 1: 写失败测试**

```cpp
TEST(EngineManifest, RejectsOnnxMismatch) { /* Err 含 onnx_sha256 */ }
TEST(EngineManifest, RejectsTrtVersionMismatch) { /* Err 含 trt_version */ }
TEST(EngineManifest, RejectsGpuMismatch) { /* Err 含 gpu_name */ }
TEST(EngineManifest, AcceptsExactMatch) { /* ok */ }
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build -j && ctest --test-dir build -R EngineManifest --output-on-failure`
Expected: 编译失败

- [ ] **Step 3: 实现 `checkEngineCompatible` 并通过测试**

Run: `ctest --test-dir build -R EngineManifest --output-on-failure`
Expected: PASS

- [ ] **Step 4: 实现 Windows 代码**

- `env_probe`：输出 `config/toolchain.lock.json`（GPU、显存、驱动、CUDA、TensorRT、MSVC、实际库名）。
- `engine_builder`：`--onnx --manifest --precision fp32|fp16 --out`，输出 `model.engine`、`engine_manifest.json`。
- `TensorRtDetector::create`：先调 `checkEngineCompatible`，再对引擎实际输入输出调 `validateOutputShape`。
- `infer`：letterbox、归一化、上传、推理、下载、`decodeYolo`，RAII 管理 CUDA 资源。

Run: `python3 tools/check_boundary.py . && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: macOS 构建不受影响，全部通过

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add TensorRT env probe, engine builder and detector"
```

### Task 9: 真实视频输入与可视化

**Files:**
- Create: `include/vision/io/{VideoFileSource,VideoOverlaySink}.hpp`、`src/io/{VideoFileSource,VideoOverlaySink}.cpp`
- Modify: `apps/replay_runner/main.cpp`（接入 `--source video` 和 `output.write_video`）
- Test: `tests/replay/test_video_source.cpp`

**Interfaces:**
- Consumes: `IFrameSource`、`IResultSink`、`Frame`
- Produces: `Result<std::unique_ptr<VideoFileSource>> VideoFileSource::open(path, std::optional<path> timestamps_csv)`；`VideoOverlaySink(path, fps, IResultSink& next)`。旁路文件 `<video>.timestamps.csv`，列为 `frame_id,capture_ns,focus_state`（可选列，取值 `unknown|stable|changing`，缺失时为 `unknown`）。两者仅在 `VISION_WITH_OPENCV=ON` 时构建。

- [ ] **Step 1: 写失败测试**

```cpp
TEST(VideoSource, MissingFileFails) { /* Err 含路径 */ }
TEST(VideoSource, ZeroFrameVideoFails) { /* 结合 Pipeline → Err “无可读帧” */ }
TEST(VideoSource, UsesSidecarTimestamps) { /* capture_ns 与 csv 一致，timestamp_estimated=false */ }
TEST(VideoSource, ReadsFocusState) { /* csv 标 changing 的帧 → focus_state==Changing；无此列 → Unknown；非法取值 → Err */ }
TEST(VideoSource, EstimatesWithoutSidecar) { /* 第 n 帧 = n*1e9/fps，timestamp_estimated=true */ }
TEST(VideoSource, CorruptFrameStopsCleanly) { /* 截断文件 → 已读帧正常输出，summary 记录提前结束 */ }
```

测试视频由 OpenCV `VideoWriter` 在测试中生成。

- [ ] **Step 2: 运行确认失败**

Run: `cmake -S . -B build-cv -DVISION_WITH_OPENCV=ON && cmake --build build-cv -j && ctest --test-dir build-cv -R VideoSource --output-on-failure`
Expected: 编译失败。若本机缺 OpenCV，用 `brew install opencv` 前先询问用户。

- [ ] **Step 3: 实现**

- [ ] **Step 4: 运行确认通过**

Run: `ctest --test-dir build-cv -R VideoSource --output-on-failure`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add video file source and overlay output"
```

### Task 10: 报告工具与 Windows 运行包

**Files:**
- Create: `apps/eval_report/main.cpp`、`scripts/win/phase1_run.ps1`、`docs/windows-runbook.md`
- Modify: `README.md`
- Test: `tests/replay/test_eval_report.cpp`

**Interfaces:**
- Consumes: `results.jsonl`、`summary.json`、`toolchain.lock.json`、`LatencySummary`
- Produces: `eval_report --run <dir> --out report.md`，生成中文报告：环境、模型、配置哈希、帧数、丢帧数、各阶段 P50/P95/P99、显存峰值（缺失时写“未采集”）、检测框按短边 `<16`、`16–32`、`32–96`、`>96` 的分布，`stable`、`changing`、`unknown` 三种对焦状态的帧数和检测数，以及未完成项。

- [ ] **Step 1: 写失败测试**

```cpp
TEST(EvalReport, ContainsRequiredSections) { /* 输入 Task 6 的产物，报告含“P95”“配置哈希”“未完成项” */ }
TEST(EvalReport, MissingLockIsExplicit) { /* 无 lock 文件 → 写“未在目标 GPU 上验证”，不编造 */ }
TEST(EvalReport, GroupsByFocusState) { /* 含 changing 帧的结果 → 报告单列帧数和检测数 */ }
TEST(EvalReport, BucketsBySize) { /* 15/20/50/100 px 的框各计入对应分桶 */ }
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build -j && ctest --test-dir build -R EvalReport --output-on-failure`
Expected: 编译失败

- [ ] **Step 3: 实现 `eval_report`、PowerShell 脚本和 runbook**

`phase1_run.ps1` 依次执行：构建、探针、`engine_builder`、黄金样本比对、`replay_runner --detector trt`、`eval_report`。任一步失败立即停止并打印日志路径。回放期间脚本用 `nvidia-smi --query-gpu=memory.used --format=csv -l 1` 采样，把峰值写入 `gpu_memory.csv`，供 `eval_report` 读取。`windows-runbook.md` 写明前置条件和需要发回的文件。

- [ ] **Step 4: 全量验证（macOS）**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure && python3 tools/check_boundary.py . && (cd python && uv run pytest tests -q)`
Expected: 全部通过。在报告中注明 Windows GPU 部分待用户运行。

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add evaluation report and Windows run package"
```

### Task 11: 实时最佳照片筛选

随视频流运行，按时间窗保存最佳原始帧。只读取已采集的帧并写本地文件，不连接任何设备，不控制相机、机械臂或快门。阶段 1 用回放验证，阶段 2 接入相机后走同一接口。

**Files:**
- Create: `include/vision/core/BestShot.hpp`、`src/core/BestShot.cpp`
- Modify: `include/vision/core/Pipeline.hpp`、`src/core/Pipeline.cpp`、`apps/replay_runner/main.cpp`、`include/vision/core/Config.hpp`、`src/core/Config.cpp`、`config/default.yaml`、`docs/tuning.md`、`docs/windows-runbook.md`
- Test: `tests/unit/test_best_shot.cpp`

**Interfaces:**
- Consumes: `Detection`、`Frame`、`AppConfig`、`loadConfig`、`runPipeline`
- Produces: `struct BestShotConfig { bool enabled{false}; int target_class{0}, window_ms{1000}, edge_margin_px{4}; float min_confidence{0.5f}, position_min{1.f/3}, position_max{2.f/3}, min_sharpness{0.f}; }`；`enum class RejectReason { None, LowConfidence, TouchesEdge, OutOfPosition, TooBlurry }`；`RejectReason checkGeometry(const Detection&, int width, int height, const BestShotConfig&)`；`double sharpness(const uint8_t* gray, int width, int height, int step, RectF roi)`；`struct ShotCandidate { uint64_t frame_id; TimestampNs capture_ns; RectF bbox; float confidence; double sharpness; }`；`class RollingBestShot`，构造 `RollingBestShot(BestShotConfig)`，方法 `std::optional<ShotCandidate> offer(const Frame&, const std::vector<Detection>&)`（窗口结束时返回该窗口最佳帧）、`std::optional<ShotCandidate> finish()`、`const std::vector<uint8_t>& lastSavedImage() const`、`RejectCounts counts() const`；`class IFrameObserver { virtual void onFrame(const Frame&, const std::vector<Detection>&) = 0; }`；`runPipeline` 增加参数 `IFrameObserver* observer = nullptr`。
- 输出：`shots/<frame_id>.png`（需 `VISION_WITH_OPENCV=ON`）和逐行 `best_shots.jsonl`（帧号、时间、框、各项得分、窗口拒绝计数）。

设计中的“遮挡判定”在第一版没有独立模型，用 `min_confidence` 近似，并在 `tuning.md` 写明这一局限。排序只按清晰度：先过硬门槛，再取窗口内清晰度最高的帧。窗口按 `capture_ns` 切分，不用墙钟时间，保证回放可复现。观察者运行在输出线程，不占用推理线程；只有当前帧更优时才复制图像，每个窗口最多保留一帧。观察者跟不上时丢弃观察并计数，不阻塞采集或推理。

- [ ] **Step 1: 写失败测试**

```cpp
TEST(BestShot, AcceptsCenteredWholeTarget) { /* 1440×1080，框 (600,450,240,180)，conf=.9 → None */ }
TEST(BestShot, RejectsTouchingEdge) { /* x=2 → TouchesEdge */ }
TEST(BestShot, RejectsOutsideMiddleThird) { /* 中心 x=300 → OutOfPosition */ }
TEST(BestShot, BoundaryIsInclusive) { /* 中心 x=480、x=960 → None */ }
TEST(BestShot, RejectsLowConfidence) { /* conf=.3 → LowConfidence */ }
TEST(BestShot, SharpScoresHigherThanBlurred) { /* 棋盘格图 > 其模糊版本 */ }
TEST(BestShot, FlatImageScoresZero) { /* 纯色 → 0 */ }
TEST(BestShot, RoiClippedToImage) { /* 超界 ROI 不越界读取，ASan 干净 */ }
TEST(BestShot, EmitsBestFramePerWindow) { /* window=1000ms，1s 内 3 个合格帧 → 窗口结束时返回最清晰的一帧 */ }
TEST(BestShot, WindowUsesCaptureTime) { /* 处理快慢不同但 capture_ns 相同 → 结果相同 */ }
TEST(BestShot, EmptyWindowEmitsNothing) { /* 无合格帧 → nullopt，counts 记录拒绝原因 */ }
TEST(BestShot, FinishFlushesPartialWindow) { /* 流结束时 finish 返回未满窗口的最佳帧 */ }
TEST(BestShot, IsDeterministicOnTies) { /* 同分时取较小 frame_id */ }
TEST(BestShot, SavedImageIsOwnedCopy) { /* 原帧释放后 lastSavedImage 内容不变 */ }
TEST(BestShot, ObserverNeverBlocksPipeline) { /* 慢观察者下推理帧数不变，丢弃数 > 0 */ }
```

- [ ] **Step 2: 运行确认失败**

Run: `cmake --build build -j && ctest --test-dir build -R BestShot --output-on-failure`
Expected: 编译失败

- [ ] **Step 3: 实现**

清晰度用目标框内灰度图 3×3 拉普拉斯响应的方差；框小于 3×3 时返回 0。`replay_runner` 在 `best_shot.enabled=true` 时启用观察者；无合格帧时正常退出，`summary.json` 写明各原因的拒绝数。给配置增加 `best_shot` 段，按 Global Constraints 校验范围，要求 `position_min < position_max`。

- [ ] **Step 4: 运行确认通过（含 ASan）**

Run: `cmake -S . -B build-asan -DCMAKE_CXX_FLAGS=-fsanitize=address && cmake --build build-asan -j && ctest --test-dir build-asan -R BestShot --output-on-failure && python3 tools/check_boundary.py .`
Expected: PASS，无内存错误

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat: add real-time best shot selection"
```

---

## 本计划之后

阶段 1 只有在 Windows 上跑通 `phase1_run.ps1` 并在实验台真实录像上复测后才算完成。阶段 2（运动补偿、Kalman、IMM、MOT、预测）另写计划，届时根据真实数据和第 16 节默认值的确认结果调整。
