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
