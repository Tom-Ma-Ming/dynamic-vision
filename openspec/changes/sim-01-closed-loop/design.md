# 设计

## 世界与时间

屏幕 `width x height`，时间步 `dt_s`（默认 0.04s，25 FPS）。世界坐标与相机偏移分离：`screen = world - camera`。目标有中心、宽高、速度；相机平移只改变 `camera.x`，不改世界真值。

运动模式：

- `constant`：匀速，边界反弹；
- `stop`：中途速度归零；
- `turn`：中途反向/变向；
- `squat`：底边固定，高度收缩，中心随之下移；
- `occlusion`：指定帧区间不产生观测，真值继续运动；
- `camera_pan`：相机匀速移动，屏幕目标随之移动。

## 观测

每帧从屏幕真值生成 person 框，可选 head 框；加高斯噪声。`dropout` 控制漏检概率，`occlusion` 区间不产生任何真值框；`false_positive_rate` 生成远离真值的随机框。检测分数固定 0.9，类别 0=head、1=person。

## 估计与预测

`mot::Tracker` 接收每帧 `mot::Detection`；选择距屏幕真值最近的 `Confirmed/Lost` 轨迹作为当前目标，记录 track id、状态、ambiguous、age、协方差迹。预测位置 = 估计中心 + 估计速度 × `command_horizon_s`。

## 执行器

命令先进入 `actuator_delay_frames` 长度的延迟队列；执行器以 `actuator_max_speed` 向命令位置移动，单步位移受 `max_speed*dt` 限制。初始 aim 在画面中心。

## 位置闭环

`e_norm = sqrt((ex/width)^2 + (ey/height)^2)`。

有效条件：选中 Confirmed 轨迹、非 ambiguous、本帧有观测、`age <= max_age_ms`、`covariance(0,0)+covariance(1,1) <= covariance_limit`。

`e_norm < epsilon_enter` 连续 `settle_frames` 帧 → SUCCESS。`e_norm > epsilon_exit` 清零计数，形成滞回。

## 结果闭环

- SUCCESS：位置闭环满足并产生终止帧；
- FAILED：超时且期间存在有效轨迹，按 ID 切换/预测误差/控制饱和归类；
- UNKNOWN：连续 `unknown_grace_frames` 没有有效轨迹/观测（遮挡、漏检、身份歧义）；不得归为 FAILED。
- 每次运行只有一个终止结果，不重试；日志记录终止帧、原因、每帧真值/观测/估计/预测/aim/误差。

## 指标

MAE/P95（像素，有效帧）、稳定时间、超调、遮挡恢复时长、观测年龄 P50/P95/P99、检测数/误检/漏检、ID 切换、结果状态。`sim_runner` 按 motion 和尺寸桶聚合。

## 依赖

`vision_sim` 链接 `vision_mot`/OpenCV；不进入 `vision_core`，不依赖 TensorRT。只在 `windows-cpu`/`windows-full` 构建。

## 回退

仿真完全独立于检测/推理服务；删除 `apps/sim_runner` 与 `vision_sim` 即可回退，不影响录像链路。
