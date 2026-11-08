# 图像观察与 TensorRT 仿真集成 · 2026-11-08

新增 ImageObservation：由场景内部按世界/相机位置渲染 BGR 画面；检测回调仅收到图像、帧编号和模拟时间，不接收真值框。图像路径关闭合成噪声框/假阳性注入，空检测不会回退真值。人物采用矩形裁片粘贴，背景平移使用 wrap；遮挡时隐藏人物，边界裁切可见区域。这是二维工程夹具，不是逼真环境。

vision_sim_image 在 windows-sdk 构建，复用当前 Detector、预处理、逐类 NMS 和坐标恢复。固定 320×320/25 FPS/100 帧静止人物场景，动作驱动相机偏移，使用理想模拟编码器补偿；PID kp=2，其余参数保持默认。输出 scene.avi、detections.jsonl、feedback.jsonl。当前同步推理耗时尚未进入模拟时钟，不能用它声明实时延迟已验收。

复现（先按 image-replay.md 设置依赖 PATH）：

```powershell
cmake --build --preset windows-sdk --target vision_sim_image
out/windows-sdk/apps/trt_infer/Release/vision_sim_image.exe artifacts/engines/yolo11n-v002-fp32/best-fp32.engine artifacts/sim/image-assets-v1/sprite.png artifacts/sim/image-assets-v1/background.png configs/tracker-imm.yml artifacts/sim/image-trt-new
```

输入人物来自冻结复核 clip01 frame 4019 最大 person 框，135×78 裁片，灰色背景；来源矩形见 image-assets-v1/source.json。该样本只用于工程集成，不作为独立测试或训练新增数据。未修改原始标注/图像。

CPU CTest 27/27：像素分割测试通过渲染图像闭环并收敛；空图像检测返回时始终无轨迹，证明没有真值兜底。SDK 新程序构建成功。真实 Engine 100 次调用、100 帧视频完整解码，摄像机偏移改变后续观察；结果 UNKNOWN、MAE=21.9574 px、轨迹覆盖 .99。末帧 camera=(-39,-16)，真实投影=(149,176)；检测存在同一 person 对应两个 head 的候选，需进一步定位歧义门控。证据 artifacts/sim/image-trt-v1/verification.json 与逐帧日志/视频。

完成的是图像→真实检测→跟踪→仿真执行→新图像的首个工程回路。尚缺多场景/多人物素材、真实感背景、推理与反馈延迟事件、完整命令报告、独立真值验收及最终控制成功标准。下一步扩充日志定位 UNKNOWN，处理头身歧义与控制偏差；不将本次集成通过等同整个平台完成。

## 门控诊断与头身歧义实验

补齐 feedback.jsonl 的 control_gate、track_state、covariance_trace 和估计位置；增加 commands.jsonl、summary.json 及真实 detect_ms（不参与模拟时钟）。基线 image-trt-diagnostic-v2：82 ambiguous、17 ready、1 no_selected_track，第 67 帧进入 UNKNOWN。

新增 tracker-body-primary.yml，开启 separate_head_ambiguity（默认关闭）。同一人物多个头候选时，清除头部测量并标记 headAmbiguous，保持明确人物框；跨人物归属歧义仍拦截，冲突头锚点不能参与后续头/身转换。它不是简单忽略所有歧义，也不选分数最高的头冒充确定。

CPU 27/27。图像对照 image-trt-body-primary-v1：99 ready、1 no_selected_track，结果 FAILED、MAE21.9088、覆盖.99；只消除了 UNKNOWN 的原因，尚未解决检测框中心与真实人物区域偏差。下一步需检查预测/目标点的语义以及各类检测误差，多素材测试；不修改真值或放宽阈值以追求通过。

## 固定取样素材诊断

tools/run_image_loop_suite.py 固定取 clip01/02 的第15、45、75项，每帧使用最大标注 person，不根据模型效果替换样本。两个位置没有人物，记录跳过；四个有效裁片各运行100帧并完整解码，报告保留标注/原图/引擎/程序/配置哈希。矩形读取支持 X-AnyLabeling 两点和四点，2项工具测试通过。

证据 artifacts/sim/image-suite-v2/report.json：大人物裁片检测100/100帧，中心Y偏差−17.465px，最终误差15.033px、FAILED；三个小人物裁片全部无检测、UNKNOWN。不能因此认定模型原场景召回为零：original-context-comparison.json 对同一原图重新推理，三个小人物与原标注 IoU 分别 .916/.925/.881，原图均能检测；灰背景紧裁片破坏上下文造成明显分布变化。大人物原图检测下沿216.9，复核框下沿约259.1，原图也存在人物框偏短。

下一步渲染应保留上下文并明确人物框在素材中的坐标；不能通过紧裁片失败直接触发模型重训，也不能把人物裁片的周围背景算入人物真值。当前图像套件为诊断，不是代表性测试。

## 上下文素材与显式选择

ImageObservation 增加 sprite_target，单独表示人物框在素材中的位置；空值兼容紧裁片。渲染缩放/位移以该 ROI 为锚点，背景不计入人物真值。新增像素分割回归证明带上下文后真值锚点正确且能够收敛；CPU27/27、SDK目标构建通过。

vision_sim_image 的原有5个位置参数后可追加 target-x target-y target-width target-height，再可追加 selected-track-id。不追加 ID 时沿用唯一明确候选首次选择；显式 ID 沿用现有 Selection，不用真值挑选/重选。summary 记录选择模式与 ID。

使用 clip02 frame555 原图全幅上下文，ROI=(162.463165,192.33812,14.20575,25.137237)，首帧 person 检测恢复且与原图一致。唯一候选模式因为两个人物而无选择，UNKNOWN（image-context-v1）；显式选择 ID1 后100帧完整输出解码并发生视角反馈，覆盖.99，MAE7.68974，FAILED（image-context-selected-v1）。这确认紧裁片漏检不能作为原模型召回证据，但完整控制精度仍待改进。

当前全幅素材中的其它人物随整幅背景刚性移动，只是上下文工程测试；未构建独立多人物动力学。完整延迟时钟、量化/标定和更广泛图像验收仍待后续。

## 量化与渲染分辨率对照

vision_sim_image 可在 selected-track-id 后追加 units-per-pixel 与 subpixel-render（0/1）。分辨率只改变模拟执行单位到像素的映射，保持物理最大速度和 PID 误差尺度一致；不是实际鼠标/硬件标定。亚像素模式用仿射双线性采样及覆盖率合成，避免原整数粘贴把小于1像素的动作隐藏；默认关闭，整数模式兼容。

固定 clip02 frame555、ID1、同一引擎和 tracker-body-primary，六组100帧均完整解码。证据 artifacts/sim/quantization-v1/report.json：

| 单位/px | 亚像素 | 全窗口 MAE px | 最后误差 px | 结果 |
|---:|---|---:|---:|---|
| 1 | 关 | 7.690 | 1.069 | FAILED |
| 1 | 开 | 7.690 | 1.069 | FAILED |
| 4 | 关 | 7.674 | 1.442 | FAILED |
| 4 | 开 | 7.679 | 1.159 | FAILED |
| 16 | 关 | 7.608 | 1.407 | FAILED |
| 16 | 开 | 7.723 | 1.296 | FAILED |

门控均99 ready/1 no_selected_track。CPU27/27，含亚像素上下文锚点测试；SDK目标构建通过。分辨率提高未带来一致收益，不能据此选择更细步长宣称完成。当前检测和真值的像素级偏差仍限制严格小目标判据；不放宽阈值、不移动真值。下一阶段优先建立观察发布延迟/时钟模型，保持精度缺口显式存在。

## 观察采集与发布时钟

新增 observation_delay_ms（0～10000ms，默认0），图像/检测携带采集帧号和模拟采集时刻，进入延迟发布队列。到期后按采集时间更新 tracker，而不是将旧结果改写为当前结果。控制使用 predictAt(now)；模拟编码器额外补偿采集到当前之间已经实际执行的增量，避免把旧图像坐标直接作为当前坐标。IMM 后续外推同样应用该位移。观测年龄超过 max_age_ms 时禁止生成命令。

vision_sim_image 可在 subpixel-render 后追加 observation-delay-ms。feedback 增加 observation_frame/observation_ts_ns，-1表示尚无已发布结果。图像检测仍同步计算，配置延迟是模拟发布安排；真实 detect_ms 单独测量，不代表全部耗时已进入模拟时钟。

CPU27/27；固定80ms测试验证前两帧不可见结果、后续来源帧为当前帧减2，时间戳不重写；40ms有效期面对80ms延迟不产生增量控制。图像四组 artifacts/sim/observation-delay-v1：0/40/80ms MAE7.690/8.157/8.585，均FAILED；240ms为UNKNOWN、93 stale、零控制命令。零延迟逐字段等于旧图像基线。尚缺真实推理服务时钟、可变观察延迟、丢弃计数和长时队列压力验证，不据此声明完整实时链路验收。

## 变化发布延迟与服务时间

新增 observation_jitter_ms、inference_service_ms 和 observation_capacity（默认256，范围1～10000）。独立随机流模拟发布抖动；服务时间在单服务者时间轴累积，ready=服务完成+非负发布延迟。结果按ready交付，采集时间不晚于已处理状态的结果标为stale并丢弃，状态不回退；剩余结果超容量时丢弃最旧采集项。该容量约束是结果缓冲区，不是完整真实推理输入队列，已经计入的服务工作不会因结果丢弃而撤销。

镜头反馈改为累计已应用增量，在实际发布时计算上次已接受采集到本次采集的差值，因此乱序/丢弃不会破坏补偿区间。控制再补偿最新接受采集到当前时刻的增量。

ImageObservation 可提供刚结束检测的 service_time_ms 回调。图像程序可在 observation-delay-ms 后追加 measured-service（0/1），再追加 observation-jitter-ms。实测推理时长被送入服务时间模型；这不是异步真实运行时钟，也不包括渲染、视频写入和操作反馈的全部壁钟耗时。实测模式受调度影响，不宣称字节可重复。observations.jsonl 逐帧记录capture/ready/released、service_ms及published/stale/overflow/pending。

CPU27/27，覆盖乱序不回退、发布因果、过载结果缓冲容量。observed-service-v1 用80ms基础发布延迟、±80ms抖动和实际推理耗时：100结果=61 published+35 stale+4 pending，因果/单调性审计通过；最大单次服务38.5294ms，MAE8.99388、FAILED。仅是模型驱动诊断，不是全链路实时验收。
