本轮决策：使用已有 dynamic-vision 应用。独立探针模式仅需 CMake 3.20，完整工程沿用 3.26。8.6.1.6 仅作为本机探测样本；部署版本仍待锁定。回退可移除探针子目录接入及其预设。
2026-11-07：保留原始类别 ID；参考同盘 data.yaml 的 player/head 命名。无采集记录时保留空元数据，暂停划分。
依用户重新标注约定，yolo_v001 的类别为 0=head、1=person。旧训练集仅在派生副本中交换编号，独立录制用于验证。
首个基线使用本机 YOLO11n 权重、原分辨率 320 和 FP32；ONNX 固定 batch=1、opset 17、外部 NMS。
TensorRT 8.6.1.6 本机引擎关闭 TF32，工作区 512 MiB。固定输入输出维度与类型由 runner 启动时检查。
保留原始 FP32 张量入口，复用同一个 C++ Detector，图片和录像在 OpenCV 可选构建中启用。录像使用媒体 PTS，与单调计时器测得的处理耗时分列。
采用采集时间轴上的 CV Kalman 与 Joseph 协方差更新。稳定全局一对一分配，按确认/暂失/移除控制生命周期；选择只读快照。
2026-11-07：windows-sdk 保留 probe+TRT 应用构建；windows-full 构建 vision_core/GTest；windows-cpu 构建无 TRT 的 CPU 全量测试。MSVC 统一 /utf-8；PicoSHA2 使用上游 target，不重复包装。总设计副本入库 docs/FPS_Vision_Tracking_TensorRT_Official_v2.0.md。
2026-11-07：TRACK-02 采用 blocking 默认、latest 显式 realtime；FramePacket 同时携带媒体 PTS 与 steady_clock 采集/入队时刻；观测年龄定义为 publish-capture，不含叠加写入；queue_capacity 仅允许 1/2，realtime_pace 必须配 latest；队列测试进入 vision_core/GTest，CPU 预设可独立运行。
2026-11-07：EVAL-01 报告只读聚合 artifacts，不重跑训练/构建/CTest；缺失分层标 missing/unknown；--strict 只要求必需证据存在，不设精度门槛；实景 MOT、场景分层、负样本不足明确列为缺口，不用合成指标替代实景准确率。
2026-11-07：TRT2-06 使用 pinned host 输入/输出 + 单条 CUDA stream；单帧同步路径不启用多 stream；输出指针有效期到下一次 infer；benchmark 取 3 轮中位数，mot_ms 的 0.01ms 量级差异视为噪声。
