# Grok 工程交接 · 2026-11-07

> 2026-11-07 接手复核更新：工作树已由检查点提交 `900c440` 入库；总设计已复制到仓库 `docs/FPS_Vision_Tracking_TensorRT_Official_v2.0.md`；新增 `windows-full` / `windows-cpu` 构建入口；TRACK-02 的 OpenSpec 已写在 `openspec/changes/track02-bounded-queue/`，实现尚未开始。复核与整改证据见 `docs/review-remediation.md`。

## 从这里开始

- 应用仓库和工作目录为 E:\mac-share\tensor\dynamic-vision。
- 总设计已复制到 docs/FPS_Vision_Tracking_TensorRT_Official_v2.0.md（原始文件位于 E:\mac-share\tensor\TensorRT-11.4\FPS_Vision_Tracking_TensorRT_Official_v2.0 (1).md）。先读第 28 章验收矩阵、第 29 章计划、第 33 章多目标跟踪增补，再按任务阅读对应章节。第 30 章记录首次启动要求。
- docs/progress.md 是按时间追加的实测进展；docs/decisions.md 记录决策；docs/problems.md 记录缺口。历史结论有过修订，以后续复核记录为准。openspec/changes 下有各阶段 proposal、design、tasks、acceptance；新任务格式见 openspec/README.md。
- 交接时 Git HEAD 为 c890230；2026-11-07 已补检查点提交 900c440，后续整改提交见 git log。artifacts、out、.venv 被忽略。同机请直接打开现有工作区；异机需同时取得工作树和下述数据/模型产物，单独克隆早期提交无法复现现状。请保留现有工作文件。

## 第 29、33 章任务对照

| 任务 | 当前交付与余项 |
|---|---|
| TRT2-00 / 01 | 本机探针、Windows x64 Release 预设、CTest 和 CI 配置已落地。环境见 docs/environment-lock.md。CI 已配置 Windows probe 与 Ubuntu CPU 两个 job；本机等价验证 CPU 4/4、windows-sdk 3/3，远程 Actions 尚待实际运行。 |
| DATA-01 / 02 | 已完成本批截图导入、人工头身复核、独立录制验证划分与 SHA 去重。通用全帧/ROI 迁移、完整场次元数据、多场景测试集及负样本仍待补。 |
| MODEL-01 | YOLO11n 320 FP32 基线、最佳权重、ONNX 契约和参考输出已验证。person 召回 95% 属于用户明确延后的优化工作。 |
| TRT2-02 / 03 | 本机引擎构建、摘要清单、C++ RAII 加载、固定张量契约与单图推理已跑通。更换 GPU/SDK 后需要重建；本机部署基线是 8.6.1.6。 |
| TRT2-04 / 05 | 图片 letterbox、解码、逐类 NMS、坐标还原以及顺序视频回放已完成实图对照和合成录像回归。真实录像与长时资源趋势待测。 |
| TRACK-01 / 03；MOT-01～07 | 已有原图坐标、时间戳、CV Kalman、匈牙利关联、头身合并、生命周期、独立选择、UI、JSONL、合成指标。证据覆盖交叉、遮挡、失效、乱序和重复回放。真实身份标注录像、急停/蹲下/镜头快速旋转的分层验收仍待补。 |
| **TRACK-02** | **OpenSpec 已写，待实现。现有 replay.cpp 同步解码→检测→跟踪；跟踪器已有过期结果拒绝逻辑，采集/推理解耦、有界队列、丢帧计数与观测年龄分位数尚待实现。** |
| TRACK-04 / MOT-08 | IMM、镜头补偿、低置信度二次关联或 ReID 为后续实验，需基线与消融。 |
| TRT2-06 / EVAL-01 / SIM-01 | GPU 性能优化、统一分层评估报告与自建仿真闭环仍待实施。已有局部脚本与报告可复用。 |

## 关键输入与验收证据（相对于应用根目录）

- 人工复核源为 artifacts/xanylabeling-review-v1/{train,val}；训练入口 artifacts/datasets/yolo_v002/dataset.yaml。402 张训练图、42 张独立录像来源的验证图，0=head，1=person，人物大框包含头部。旧版 yolo_v001 已撤销训练资格。保留人工 JSON。
- artifacts/runs/yolo11n-v002-320/ 包含 baseline.json、person-diagnosis.json、weights/best.pt、weights/best.onnx、weights/onnx-contract.json、weights/onnx-reference.npz。验证集只有一个场次，全部有人。
- artifacts/engines/yolo11n-v002-fp32/ 有 best-fp32.engine、manifest.json、verification.json、image-check.json。输入 FP32 RGB 1×3×320×320；输出 1×6×2100；外部逐类 NMS。
- artifacts/replay/mot-v1/ 有 fixture.avi、first/frames.jsonl、first/overlay.mp4、selection-demo/、synthetic-metrics.json。夹具由 200 张采集截图按 25 FPS 循环生成，适用于工程回归。跨帧真值指标来自另一条 36 帧合成序列：HOTA 0.9370、IDF1 0.9714、IDSW 0。
- 源码入口 apps/trt_infer/replay.cpp、src/tracking/tracker.cpp、configs/tracker.yml。构建与运行命令见 docs/image-replay.md，跟踪测试与复核操作见 docs/mot-replay.md。按前者设置 OpenCV/CUDA/TRT 的 PATH 后执行 cmake --build --preset windows-sdk 和 ctest --preset windows-sdk；启用 OpenCV 的 windows-sdk 最近一次验收为 3/3。完整构建（含 vision_core/GTest）用 windows-full，无 TRT 的 CPU 全量测试用 windows-cpu；两者都需要追加 -DOpenCV_DIR=F:/opencv/opencv/build。

## 下一次迭代的交付要求（TRACK-02）

1. 先写 OpenSpec。保留顺序、逐帧、可复现的离线评估模式，新增可选实时节奏回放模式。
2. 用有界队列交付帧 ID、图像所有权、媒体时间和采集单调时钟时间。队列满时仅替换尚未消费的旧帧。按顺序关闭生产与消费，覆盖输入结束和推理异常。
3. 分开统计排队等待、处理耗时、发布时观测年龄、丢弃数和队列峰值。单调时钟计算延迟，媒体 PTS 用于录像运动时间轴。
4. 用可控慢消费者验证 T07、T08；报告容量、处理/丢弃/失败的帧数与 P50/P95/P99；验证旧结果不会改变新轨迹。重跑同步模式 1000 帧回归、原有 CTest。记录命令、机器、实际结果并更新进度与问题文档。
5. 继续遵守离线录像/授权测试/自建仿真范围。每阶段验收后再推进。当前无需用户再标注或重新训练。

## 异机交接

如接手方只有聊天框，请先提供本文件和 v2.0 总设计，让其明确是否可读写工作区。要实际开发，可提供包含当前未跟踪源码、配置、测试、OpenSpec 和 docs 的项目快照；要运行回归，再提供 ONNX、验证夹具、参考输出及清单。采集数据和人工标注按需要、经用户决定后转移。不同机器上重建 Python 环境和 Engine，核对 dataset.yaml 的本机绝对路径。此文档仅保存在本机。
