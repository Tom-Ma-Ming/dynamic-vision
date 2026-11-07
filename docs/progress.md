2026-11-06：TRT2-00/01 本机环境记录与独立探针 Release 构建完成。windows-base 2/2，windows-sdk 2/2。联网业务依赖、模型与 Engine 未参与本轮测试。后续先做 DATA-01/02，再进入模型与推理链路。
2026-11-07：DATA-01/02 首批只读导入。402 对 320×320 图片和标签，结构校验通过。复现：py -3.13 tools/import_screenshots.py H:/dataset/labels/images/train artifacts/data-intake --existing-splits H:/dataset/dataset/images（需 Pillow）。缺标签、空标签及越界框回归测试通过。
用户确认 data_collect 为独立录制。其 200 张 JPG 全部预留验证，首轮按时间等距抽取 24 张供人工标注，目录 artifacts/validation-20261102-000654/annotation-round-01。其余帧也应排除在训练集外。
yolo_v001：已导出 402 对训练样本与 42 对验证样本，逐项比对原图字节、标签行数、坐标及类别映射，444 对通过，跨集 SHA-256 重合为零。
纠正上一轮验收：444 对仅通过数据复制及编号校验。旧训练集需重画或复核人物框，尚未训练。新集 59 个头框中 35 个有边界外伸，清单见 containment-review.csv。
已生成 artifacts/box-review-v1 候选标注副本及逐图双栏预览。444 对源图与候选图逐字节一致，所有头框坐标保留；47 张优先人工复核，未训练。
已从人工复核后的 X-AnyLabeling JSON 生成 yolo_v002：train 402 图（head 418/person 415），val 42 图（各 59 框）。重复图与结构检查通过，边界例外见 containment.csv，源标注保留。
MODEL-01：yolo_v002 / YOLO11n，320 输入、batch 8、GTX 1060，训练 17 轮后早停。最佳权重复验和三张实图 ONNX Runtime 对照通过。记录：artifacts/runs/yolo11n-v002-320/baseline.json、weights/onnx-contract.json。
TRT2-02：FP32 Engine 已生成，C++ 单样本 runner 通过三张实图、8 个框的 ONNX 对照。person 召回专项报告和漏检图保存到基线目录。
TRT2-04/05：C++ 实图与 ONNX 5 图 13 框通过，最大坐标差 <0.001 px；基于 200 张图片的 1000 帧录像夹具两轮回放完成，逐帧检测/时间戳一致，输出视频逐帧可解码。见 docs/image-replay.md。
完成离线 MOT 基线。CTest 3/3。双人合成序列 HOTA 0.9370、IDF1 0.9714、IDSW 0；两轮 1000 帧轨迹一致，按 ID 选择/切换/清除时逐帧轨迹保持一致。使用见 docs/mot-replay.md。
2026-11-07：接手复核与整改。新增 windows-full/windows-cpu 预设，vision_core/GTest 进入 full/cpu 构建；修复 PicoSHA2 重复目标与 MSVC /utf-8；windows-cpu 等价配置 CTest 4/4，windows-sdk 3/3，pytest 5/5；重建 vision_replay 的 1000 帧输出与既有证据逐字段一致。TRACK-02 OpenSpec 已写，未实现。详见 docs/review-remediation.md。
2026-11-07：TRACK-02 完成。新增 BoundedQueue（blocking/latest，容量 1/2）、LatencyStats、FramePacket/ReplayConfig；vision_replay 改为采集/推理异步流水线，默认 blocking，replay-realtime.yml 启用 latest+媒体节奏。CPU CTest 20/20，windows-sdk 3/3，pytest 5/5；blocking 1000 帧与既有轨迹逐字段一致；realtime 25 FPS 1000 采集/1000 处理/0 替换，500 FPS 过载 1000 采集/250 处理/750 替换，时间轴与计数不变量通过。证据 artifacts/track02/verification.md。
2026-11-07：EVAL-01 完成。tools/eval_report.py 只读聚合检测/Engine/MOT/回放/测试证据，输出 JSON/Markdown/HTML，支持 --strict；真实 artifacts 报告为 complete-with-gaps，gates 6 pass/3 partial/3 missing，person Box-R 0.7119、合成 MOT HOTA 0.9370、7 项缺口。pytest 8/8。证据 artifacts/eval-report/，使用见 docs/eval-report.md。
