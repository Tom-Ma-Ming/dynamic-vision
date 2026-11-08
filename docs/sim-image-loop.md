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
