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
2026-11-07：EVAL-01 完成。tools/eval_report.py 只读聚合检测/Engine/MOT/回放/测试证据，输出 JSON/Markdown/HTML，支持 --strict；真实 artifacts 报告为 complete-with-gaps，当时 gates 6 pass/3 partial/3 missing（SIM-01 接入后 7/3/2），person Box-R 0.7119、合成 MOT HOTA 0.9370、7 项缺口。pytest 8/8。证据 artifacts/eval-report/，使用见 docs/eval-report.md。
2026-11-07：TRT2-06 完成。Detector 改用 cudaHostAlloc pinned 输入/输出，去掉每帧输入/输出 vector 分配；同机 3 轮 blocking benchmark result_age P50 14.34→12.90 ms（-10.1%）、infer P50 3.98→3.53 ms（-11.1%）；vision_detect、trt_infer 原始输出、1000 帧稳定字段、verify_replay 哈希均不变；pytest 10/10。单帧同步未启用多 stream。见 docs/performance.md。
2026-11-07：SIM-01 完成。新增 sim::Simulator、sim_runner、sim_test、configs/sim.yml；12 个二维场景（匀速/急停/变向/蹲下/遮挡/相机平移/小目标/大目标/误检/低置信度/无观测/执行器过慢）SUCCESS 10、FAILED 1、UNKNOWN 1，全部终止；T12 通过，EVAL gates 7/3/2。证据 artifacts/sim/sim-01/，见 docs/sim-report.md。
2026-11-07：MOT-08 低置信度二次关联完成。Config 增加 byte_track/high_score/low_score（默认关闭），Tracker 增加 high/low 两阶段关联，low 不 birth、不确认 Tentative；SIM low_conf 基线 MAE 68.73→65.44、稳定 0.48→0.64，状态均为 SUCCESS 10/1/1；CTest 21/21，默认禁用。见 docs/mot-ablation.md。
2026-11-07：修复评审五项缺陷并验收。CPU 23/23、SDK 3/3、pytest 11/11，1000 帧双跑一致；60 秒间隔下消费异常 0.390 秒退出。固定窗口仿真 v2 基线 0/8/4、ByteTrack 0/7/5，旧成功率/消融结论撤回。详见 docs/review-fixes.md。
2026-11-07：完成仿真首次选择与延迟匹配控制，CPU 23/23、pytest 11/11。三种子机动 CV 对照使变向控制 MAE 95.652→8.447 px、覆盖率 58.8%→99.3%，小目标误差略增；实验参数默认未启用。见 docs/sim-control-baseline.md。
2026-11-07：IMM 默认关闭实验完成。stationary/CV/maneuver 混合、两阶段关联、模式概率、预测快照与仿真外推已接入；CPU 25/25、SDK 5/5、pytest 11/11，开/关各双跑 1000 帧，关闭结果等于旧基线。三个 seed 下急停/变向优于两种 CV，但小目标平稳段退化，严格成功仍每组 1/10/1；未启用为默认。见 docs/imm-experiment.md。
2026-11-07：接入用户 4 段连续录像，320×320/30 FPS，共 21,169 帧；CV/IMM 8 次回放均通过全量完整性和同检测对照。抽样发现背景误检与镜头运动后轨迹漂移，暂不能推导真实身份精度。已生成两个各 90 帧的 X-AnyLabeling 候选框复核包及三栏视频，等待人工身份标注。见 docs/real-recording-intake.md。
2026-11-08：接收用户 180 帧人工 JSON，PNG 与原视频完全一致；发现 11 处类别/身份/轻微越界问题，已在独立 corrected-candidate 副本修正，原文件保留。副本结构错误 0；新增审计测试 3/3。抽样 person 未覆盖可见腿脚，框范围与身份语义仍待验收，未训练或计算正式 MOT 指标。见 docs/reviewed-label-audit.md。
2026-11-08：按用户确认读取原 review/clip01、clip02；clip01 的 34 帧框范围已更新，两组结构错误 0。冻结 180 帧并完成带标注质量限制的诊断评分：person P/R=74.37%/88.79%，head=77.22%/53.74%；IMM 两片段收益不一致，保持默认关闭。相关工具 20/20，未训练、未宣称最终真值或整体精度达标。见 docs/reviewed-clip-evaluation.md。
2026-11-08：完成背景平移补偿第一版，默认关闭；相位初值 + 局部前后向光流及空间共识门控，接入 CV/IMM/ByteTrack。CPU 26/26、SDK 6/6、Python 20/20；开关各双跑 1000 帧，关闭兼容旧基线；两段原录像 10800 帧对照通过。clip02 IDSW 24→15，但 clip01 HOTA 略降、全片门控通过仅约 27%/39%。见 docs/camera-motion-experiment.md，整个计划见 docs/task-roadmap.md。
2026-11-08：按用户指定阅读旧 tensorRT_Pro-main/src/application/auto_ai 全链路，记录类别倒置、旧头部几何、预测时间单位、延迟归属、并发命令及 PID 重置问题；完成仿真接入接口与验收草案，未修改或运行旧项目，未宣称已接通视觉闭环。见 docs/auto-ai-simulation-review.md。

2026-11-08：继续总路线图，建立 sim-actuator OpenSpec 和独立 Control.hpp：完整增量命令、纳秒模拟时钟、延迟/过期/替换/取消、速度限幅与量化、秒制 PID。CPU CTest 27/27；尚未接入 Simulator，下一步做场景集成、动态延迟消融及视觉反馈。模块为单所有者事件循环使用，不是线程安全设备接口；命令替换时清除旧代量化余数，微小持续输入的影响待场景消融。

2026-11-08：incremental_control 实验接入 Simulator/sim_runner，包含独立种子的传输抖动、按到达时刻交付、秒制 PID 与显式执行单位换算；静止目标最终误差 <3 px、重复一致、无立即动作测试通过，CPU 27/27。修正同轨迹命令替换丢失量化余数；完整命令日志、动态场景消融、饱和计数与视觉反馈仍待完成，不宣称整体控制已验收。

2026-11-08：增量控制增加 commands.jsonl 完整命令事件；完成 36 个配对场景双跑和 5776 条命令分类审计，字节级重复一致。名义延迟补偿降低均值误差，但严格 2/14/20，未达验收；CPU 27/27。详见 docs/sim-incremental.md；下一步分解断轨/控制误差并推进观察反馈。

2026-11-08：定位默认 CV 在变向/边界反弹后原 ID 过期，输入仍有观测；同源机动 CV 3/33/0、IMM 11/25/0。接入默认关闭 view_feedback（执行改变相机偏移和下一帧投影，中心固定），CPU 27/27；36 场景 IMM 全 UNKNOWN，揭示自身动作与图像速度/控制参数耦合，尚待修复；未接入渲染/YOLO。见 docs/sim-incremental.md。

2026-11-08：加入默认关闭的模拟已应用增量反馈补偿 encoder_compensation，复用带时段检查的镜头平移更新。36 场景由 0/0/36 改为 7/29/0，双跑字节一致；CPU 27/27。仅为理想编码器接口验证，真实视觉估计、反馈延迟标定及图像级闭环仍缺。

2026-11-08：新增默认关闭反馈延迟估计与未完成命令账目，CPU 27/27。有效 18 场景成功 2→6，但 MAE 9.044→12.228，保持实验；36 初始行含重复时域配置，已去重并说明统计口径。图像渲染/YOLO 接入与控制验收仍待完成。

2026-11-08：接入 ImageObservation 图像回调及 vision_sim_image TensorRT 仿真程序。CPU 27/27，SDK 目标构建通过；真实 Engine 完成 100 帧渲染→检测→跟踪→动作→新图像回路，视频解码完整。结果 UNKNOWN、MAE21.9574、覆盖.99；末帧双 head，待定位歧义。详见 docs/sim-image-loop.md，工程集成不等于精度/实时控制验收。

2026-11-08：图像回路补齐门控原因/命令/摘要/检测耗时日志，确认 82 帧头候选冲突。新增默认关闭 separate_head_ambiguity，人物可用与头部可信分开，冲突锚点禁止头身关联；CPU27/27。图像实验 ready17→99，UNKNOWN→FAILED，MAE几乎不变，仍需解决检测与任务点偏差。

2026-11-08：新增固定取样图像回路套件，4个有效裁片400帧完整解码，2个无人位置明确跳过；工具测试2/2。发现灰背景紧裁片使小目标原图可检→合成漏检；大人物框偏短原图亦存在。保留报告，下一步使用上下文素材和独立人物ROI，避免错误归因模型召回。

2026-11-08：图像素材增加独立人物ROI锚点并支持显式选择轨迹ID；保留上下文后原小目标检测恢复。CPU27/27、SDK构建通过，显式ID1图像回路100帧完整，MAE7.68974、FAILED，仍未达验收。上下文只作工程测试，多人物运动与全链路时钟仍待完善。

2026-11-08：图像回路新增可配置执行单位/px和可选亚像素渲染，CPU27/27、SDK构建通过；1/4/16单位×两种渲染共600帧完整解码，六组均FAILED，末端误差1.07～1.44px，未证实量化是主要误差来源。下一步完善观察发布延迟/时钟，不以步长调参替代定位精度验收。

2026-11-08：分离模拟采集与检测结果发布时间，延迟队列保留原帧号/采集时间，控制预测到当前并补偿采集后已应用增量。CPU27/27，图像0/40/80/240ms时序验证；零延迟基线不变，240ms观测过期时零命令。真实服务耗时与可变观察延迟仍待接入。

2026-11-08：加入可变观察发布延迟、单服务者时间模型、乱序拒绝和有界结果缓冲；累计执行反馈保证跨丢弃帧的补偿区间一致。CPU27/27，实测服务输入图像实验61发布/35乱序丢弃/4末尾待处理，因果审计通过，FAILED。用户明确远程CI暂缓，继续本地任务。

2026-11-08：完成跨模块本地回归：CPU27/27、SDK完整构建及CTest6/6、所有已入库Python工具22/22。重整sim-actuator清单，区分已实现的模型/回路与未完成的实时调度/恢复/精度验收；远程CI暂缓。

2026-11-08：新增仿真断连/发送失败与恢复，故障期间清理旧命令并正确归因；CPU27/27，3场景双跑2SUCCESS/1UNKNOWN(control)，命令终结审计通过。审计发现并修复场景步长经float读取导致纳秒偏差，最终v2逐帧40ms精确一致。见 docs/sim-fault-recovery.md。

## 2026-11-08 命令证据审计入库

新增 tools/audit_sim_commands.py：按场景和命令ID检查生成、接受、执行与终结状态，拒绝失败后执行、缺失/重复事件及身份字段漂移；支持确定性双跑逐字节对照。fault-recovery-v2 的477条命令通过，新增11项测试包含损坏日志反例。回归 CPU27/27、SDK6/6、Python33/33。修正 sim-actuator 重复清单项。远程CI按用户安排暂缓，控制精度与长期运行仍待验收。

## 2026-11-08 头身歧义真实录像消融

两段完整录像10800帧与IMM基线检测输入完全一致，视频完整解码。身份歧义计数106→63、107→89，但已标注窗口HOTA/IDF1/IDSW完全不变，默认关闭。新增head_ambiguous/body_track日志与head?叠加，SDK构建和6项回归通过。详情见body-head-video-ablation.md；完整头部测量可用性仍未实现。

## 2026-11-08 头部测量时间契约补齐

新增 head_measured / last_head_observed_ts_ns，可靠头测量才更新时间；person单独更新、头候选或身份关联冲突、Lost及预测均不伪造新测量。覆盖CV/IMM、普通关联/ByteTrack两阶段、时间0、单独头部、共享头归属冲突与恢复。head+为本帧测量，head?为头候选冲突，head-为无本帧可靠测量；Tentative仍需下游状态门控。

完整5400帧回放与上轮body-primary基线的检测、轨迹既有字段和选择逐帧完全相同。新增测量记录1220，历史4571，从未测量2008（逐帧全部轨迹计数，含Lost，非准确率）。视频完整解码并查看3993帧叠加。证据 artifacts/real-recordings-v1/head-measurement-v1/verification.json。CPU27/27、SDK6/6、Python33/33通过；增加ByteTrack低分阶段断言后，两套构建的CV/IMM测试再次通过。此前文档“测量有效性未完成”描述由本条更新；仍不证明头部检测正确、ReID或最终控制精度达标。

## 2026-11-08 有界耗时统计与资源基线

修复回放耗时vector随运行时长线性增长：固定内存直方图、全程计数/最大值、分位数误差上界1%+2ns，原始逐帧日志保留精确复算。百万样本测试通过，CPU29/29、SDK6/6。新增monitor_replay.ps1；180.52秒5400帧实时节奏回放无丢帧，result-age精确P99=13.4531ms。三分钟资源证据不替代小时级验收，详情 replay-resource-validation.md。

## 2026-11-08 一小时资源验收启动

生成108000帧/3600秒重复录像，保存来源/拼接位置/哈希；预先固定帧数、延迟、内存门槛，添加独立audit_replay_soak.py。三分钟反例正确拒绝作为小时验收。已启动单进程小时运行，artifacts/replay-soak-hour-v1，exec session83707；未获得最终结果，不标为通过。

## 2026-11-08 小时审计证据一致性加固

运行句柄83707和实际PID24572均确认仍存活，未重启。审计新增非负整数计数、实际配置、输入文件与启动参数绑定、单PID/严格时间递增/有限资源值校验，避免负数账目、混进程、重复采样和错误配置假通过。17项新增反例及边界测试通过，已入库工具套件加新测试共50/50；三分钟样本仍按预期不满足小时门槛。未覆盖运行中的vision_replay.exe，未改变既定性能阈值。

## 2026-11-08 可选日志容量限制

新增FrameLog完整记录轮转、保留数量/每段字节上限、关闭视频开关及明确的不完整日志账目；默认离线完整模式不变。5项新CPU测试通过，独立out/windows-sdk-logcheck回放程序编译通过。运行中的小时测试原程序哈希未变；本次功能的GPU集成回放待小时测试完成后执行，不标为通过。详见replay-log-retention.md。

## 2026-11-08 命令反馈丢失实验

分离执行器实际事件与控制器收到的通知；丢包不凭真实状态更新账目。期限内未终结则锁存反馈故障、停发并取消仿真运动，UNKNOWN/control。可靠反馈SUCCESS；全部丢包/20%丢包分别第6/19帧触发故障。三场景双跑逐字节一致，222命令生命周期审计通过；默认故障场景原命令及帧行为未变。CPU34/34通过，详见sim-feedback-loss.md。encoder反馈丢失及故障恢复协议仍待实现。

## 2026-11-08 ReID数据准备审计

冻结180帧中7个片段内身份、232个人物框；137个框高度<32px，同ID缺失仅1帧，无>=0.5秒长缺失候选。新增audit_reid_labels.py保留标注摘要、隔离跨片段编号；工具测试53/53通过。向用户询问clip01编号是否曾因遮挡重新分配，等待语义确认前不生成真实身份训练对；本地接口与其他工程任务仍可继续。详见reid-data-readiness.md。小时测试句柄83707仍在运行，尚未判定通过。

## 2026-11-08 仿真反馈故障恢复

显式恢复请求必须收到当前停止/清空快照；通道恢复本身不能解锁。双跑三场景：确认成功恢复SUCCESS，未请求/确认全丢仍UNKNOWN/control；185命令审计通过，恢复后不接受旧命令，原反馈丢失场景行为未变。CPU34/34通过。这里只完成零延迟停止确认模型；延迟乱序、旧快照/epoch及设备取消失败仍待实现，见sim-feedback-loss.md。

## 2026-11-08 到期截止时间与报告时间分离

修复expired日志将报告时间回填为expires的问题。IncrementActuator在advance(t)时汇总动作/观察到期，故事件timestamp应为t，物理截止仍为expires，动作积分仍只到截止时刻。这也让反馈丢失窗口按实际通知报告时刻判断，而非倒填历史时刻。

configs/sim-expiry-clock.yml采用20ms抖动和80%发送失败以覆盖未被替换而到期的命令。before/after证据为 artifacts/sim/expiry-jitter-before-v1、expiry-jitter-after-v1；33次到期报告比截止晚0.473～39.001ms，修复只改变expired的报告时间，逐帧运动、其他命令事件和指标均不变。场景仍FAILED，不把时间契约修复称为控制精度达标。

新增C++断言确保到期报告发生在实际advance调度点，Python审计拒绝截止前的到期通知，并接受跨截止区间的动作汇总报告。CPU34/34及新增断言定向复测通过，Python54/54通过；199命令生命周期审计通过。完整帧步长和动作有效期未改变。

## 2026-11-08 反馈接收顺序契约

新增独立FeedbackReceiver累计快照接收器，支持序号去重/迟到拒绝、epoch隔离、容量限制、时间与非有限值拒绝；control_contract通过。尚未接入simulator传输路径，不能宣称端到端乱序已实现。详见feedback-receiver-contract.md。小时测试最新实测仍存活，约37分钟；未覆盖运行程序。
