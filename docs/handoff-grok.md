# Grok 工程交接 · 2026-11-07

> 最新复核：旧 SIM-01 的 10/1/1 成功率与旧 ByteTrack 消融结论已撤回，见 docs/review-fixes.md。控制改进已提交 7031bb1；IMM 默认关闭实验已完成，最新 CPU 25/25、SDK 5/5、pytest 11/11，开/关各两轮 1000 帧通过。当前 IMM 三 seed 均为 1/10/1；详情和复现入口以 docs/imm-experiment.md 为准。下方其他数字保留对应历史阶段语境。

> 2026-11-07 接手复核更新：工作树已由检查点提交 `900c440` 入库；总设计已复制到仓库 `docs/FPS_Vision_Tracking_TensorRT_Official_v2.0.md`；新增 `windows-full` / `windows-cpu` 构建入口；TRACK-02 已实现并通过 CPU/GPU/1000 帧验收，EVAL-01 最新 review-fixed 报告为 6/4/2，TRT2-06 pinned memory 已完成同机前后对比，SIM-01 二维闭环在严格评分下为 0/8/4，MOT-08 ByteTrack 与 IMM 均提供默认关闭的实验开关。OpenSpec 与证据见 `openspec/changes/`、`docs/review-remediation.md`、`docs/eval-report.md`、`docs/performance.md`、`docs/sim-report.md`、`docs/review-fixes.md`、`docs/imm-experiment.md` 和 `docs/mot-ablation.md`。

## 从这里开始

- 应用仓库和工作目录为 E:\mac-share\tensor\dynamic-vision。
- 总设计已复制到 docs/FPS_Vision_Tracking_TensorRT_Official_v2.0.md（原始文件位于 E:\mac-share\tensor\TensorRT-11.4\FPS_Vision_Tracking_TensorRT_Official_v2.0 (1).md）。先读第 28 章验收矩阵、第 29 章计划、第 33 章多目标跟踪增补，再按任务阅读对应章节。第 30 章记录首次启动要求。
- docs/progress.md 是按时间追加的实测进展；docs/decisions.md 记录决策；docs/problems.md 记录缺口。历史结论有过修订，以后续复核记录为准。openspec/changes 下有各阶段 proposal、design、tasks、acceptance；新任务格式见 openspec/README.md。
- 交接时 Git HEAD 为 c890230；2026-11-07 已补检查点提交 900c440，后续整改提交见 git log。artifacts、out、.venv 被忽略。同机请直接打开现有工作区；异机需同时取得工作树和下述数据/模型产物，单独克隆早期提交无法复现现状。请保留现有工作文件。

## 第 29、33 章任务对照

| 任务 | 当前交付与余项 |
|---|---|
| TRT2-00 / 01 | 本机探针、Windows x64 Release 预设、CTest 和 CI 配置已落地。环境见 docs/environment-lock.md。CI 已配置 Windows probe 与 Ubuntu CPU 两个 job；本机验证 CPU 25/25、windows-sdk 5/5，远程 Actions 尚待实际运行。 |
| DATA-01 / 02 | 已完成本批截图导入、人工头身复核、独立录制验证划分与 SHA 去重。通用全帧/ROI 迁移、完整场次元数据、多场景测试集及负样本仍待补。 |
| MODEL-01 | YOLO11n 320 FP32 基线、最佳权重、ONNX 契约和参考输出已验证。person 召回 95% 属于用户明确延后的优化工作。 |
| TRT2-02 / 03 | 本机引擎构建、摘要清单、C++ RAII 加载、固定张量契约与单图推理已跑通。更换 GPU/SDK 后需要重建；本机部署基线是 8.6.1.6。 |
| TRT2-04 / 05 | 图片 letterbox、解码、逐类 NMS、坐标还原以及顺序视频回放已完成实图对照和合成录像回归。真实录像与长时资源趋势待测。 |
| TRACK-01 / 03；MOT-01～07 | 已有原图坐标、时间戳、CV Kalman、匈牙利关联、头身合并、生命周期、独立选择、UI、JSONL、合成指标。证据覆盖交叉、遮挡、失效、乱序和重复回放。真实身份标注录像、急停/蹲下/镜头快速旋转的分层验收仍待补。 |
| **TRACK-02** | **已实现。replay 改为有界队列的生产者/消费者流水线；blocking 默认不丢帧，realtime（latest + 媒体节奏）丢旧待处理帧。CPU CTest 20/20，windows-sdk 3/3；blocking 1000 帧与既有轨迹逐字段一致；realtime 25 FPS 1000 采集/1000 处理/0 替换，500 FPS 过载 1000 采集/250 处理/750 替换。证据 artifacts/track02/verification.md。** |
| TRACK-04 / MOT-08 | ByteTrack 与 IMM 均已实现为默认关闭的实验配置。IMM 三种子、两种 CV 对照和开关兼容回归通过；小目标仍退化，真实身份指标待测。镜头补偿、ReID 尚未实现。见 docs/imm-experiment.md。 |
| TRT2-06 / EVAL-01 / SIM-01 | pinned host buffer、统一评估聚合和二维模拟器已实现。SIM 评分已修复为完整窗口；旧 10/1/1 结论撤回，当前 IMM 严格结果每组 1/10/1。聚合旧 artifacts 的报告不能证明最新阶段精度。见 docs/review-fixes.md、docs/sim-control-baseline.md、docs/imm-experiment.md。 |

## 关键输入与验收证据（相对于应用根目录）

- 人工复核源为 artifacts/xanylabeling-review-v1/{train,val}；训练入口 artifacts/datasets/yolo_v002/dataset.yaml。402 张训练图、42 张独立录像来源的验证图，0=head，1=person，人物大框包含头部。旧版 yolo_v001 已撤销训练资格。保留人工 JSON。
- artifacts/runs/yolo11n-v002-320/ 包含 baseline.json、person-diagnosis.json、weights/best.pt、weights/best.onnx、weights/onnx-contract.json、weights/onnx-reference.npz。验证集只有一个场次，全部有人。
- artifacts/engines/yolo11n-v002-fp32/ 有 best-fp32.engine、manifest.json、verification.json、image-check.json。输入 FP32 RGB 1×3×320×320；输出 1×6×2100；外部逐类 NMS。
- artifacts/replay/mot-v1/ 有 fixture.avi、first/frames.jsonl、first/overlay.mp4、selection-demo/、synthetic-metrics.json。夹具由 200 张采集截图按 25 FPS 循环生成，适用于工程回归。跨帧真值指标来自另一条 36 帧合成序列：HOTA 0.9370、IDF1 0.9714、IDSW 0。
- 源码入口 apps/trt_infer/replay.cpp、src/tracking/tracker.cpp、configs/tracker.yml、configs/replay.yml。构建与运行命令见 docs/image-replay.md，跟踪测试与复核操作见 docs/mot-replay.md。按前者设置 OpenCV/CUDA/TRT 的 PATH 后执行 cmake --build --preset windows-sdk 和 ctest --preset windows-sdk；启用 OpenCV 的 windows-sdk 最近一次验收为 5/5。完整构建（含 vision_core/GTest）用 windows-full，无 TRT 的 CPU 全量测试用 windows-cpu；两者都需要追加 -DOpenCV_DIR=F:/opencv/opencv/build，CPU CTest 最近一次为 25/25。
- 统一评估入口 tools/eval_report.py：只读聚合检测/Engine/MOT/回放/仿真/测试证据，输出 report.json/md/html，支持 --strict 和 --sim-summary；最新 review-fixed 报告 gates 6 pass/4 partial/2 missing，旧 artifacts/eval-report 的 7/3/2 不再作为当前结论。使用见 docs/eval-report.md。
- GPU 性能入口 tools/benchmark_replay.py：同机重复回放并汇总 P50/P95/P99；TRT2-06 前后对比见 docs/performance.md，before/after 二进制与 benchmark.json 在 artifacts/bench/。
- 仿真入口 out/windows-cpu/apps/sim_runner/Release/sim_runner.exe + configs/sim.yml：12 个二维场景，输出 runs.jsonl/summary.json/report.md；见 docs/sim-report.md，证据在 artifacts/sim/sim-01/。
- MOT-08 实验配置 configs/tracker-bytetrack.yml：低置信度二次关联，默认关闭；消融见 docs/mot-ablation.md，证据 artifacts/sim/sim-02-base/、sim-02-byte/。

## TRACK-02 交付要求（2026-11-07 已完成）

> 完成情况：Q01-Q06 全部完成；当时 CPU CTest 20/20（SIM-01 接入后 21/21）、windows-sdk 3/3、pytest 5/5；blocking 1000 帧稳定字段与既有证据一致；realtime 25 FPS 1000/1000/0，500 FPS 过载 1000/250/750。证据见 artifacts/track02/verification.md。

1. 先写 OpenSpec。保留顺序、逐帧、可复现的离线评估模式，新增可选实时节奏回放模式。
2. 用有界队列交付帧 ID、图像所有权、媒体时间和采集单调时钟时间。队列满时仅替换尚未消费的旧帧。按顺序关闭生产与消费，覆盖输入结束和推理异常。
3. 分开统计排队等待、处理耗时、发布时观测年龄、丢弃数和队列峰值。单调时钟计算延迟，媒体 PTS 用于录像运动时间轴。
4. 用可控慢消费者验证 T07、T08；报告容量、处理/丢弃/失败的帧数与 P50/P95/P99；验证旧结果不会改变新轨迹。重跑同步模式 1000 帧回归、原有 CTest。记录命令、机器、实际结果并更新进度与问题文档。
5. 继续遵守离线录像/授权测试/自建仿真范围。每阶段验收后再推进。当前无需用户再标注或重新训练。

## 异机交接

如接手方只有聊天框，请先提供本文件和 v2.0 总设计，让其明确是否可读写工作区。要实际开发，可提供包含当前未跟踪源码、配置、测试、OpenSpec 和 docs 的项目快照；要运行回归，再提供 ONNX、验证夹具、参考输出及清单。采集数据和人工标注按需要、经用户决定后转移。不同机器上重建 Python 环境和 Engine，核对 dataset.yaml 的本机绝对路径。此文档仅保存在本机。
