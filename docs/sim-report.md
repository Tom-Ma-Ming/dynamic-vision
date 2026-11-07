# 自建二维仿真闭环（SIM-01）

`apps/sim_runner` 在二维世界里生成目标/相机运动、带噪声和漏检的 person/head 观测，复用 `mot::Tracker` 做估计与预测，再由一个受限执行器向预测位置移动。仿真器独立给出 `SUCCESS / FAILED / UNKNOWN`，不接触游戏、桌面或外部设备。

## 构建与运行

~~~powershell
cmake --preset windows-cpu -DOpenCV_DIR=F:/opencv/opencv/build
cmake --build --preset windows-cpu
ctest --preset windows-cpu --output-on-failure
out/windows-cpu/apps/sim_runner/Release/sim_runner.exe configs/sim.yml artifacts/sim/sim-01 configs/tracker.yml
~~~

输出 `runs.jsonl`（逐帧真值/观测/轨迹/估计/预测/aim/误差/年龄）、`summary.json`（场景、motion/尺寸桶聚合）、`report.md`。

## 场景

`configs/sim.yml` 包含 11 个场景：匀速、急停、变向、蹲下、短遮挡、相机平移、小目标、大目标、误检、无观测、执行器过慢。`success_after_frame` 让急停/变向/蹲下/遮挡/相机平移在事件发生后才判定成功。

## 闭环

- 位置闭环：归一化误差 < `epsilon_enter` 连续 `settle_frames` 帧，且轨迹 Confirmed、非 ambiguous、本帧有观测、age <= `max_age_ms`、协方差迹 <= `covariance_limit`；`epsilon_exit` 构成滞回。
- 结果闭环：SUCCESS/FAILED/UNKNOWN 每个场景只终止一次；UNKNOWN 用于无观测/长遮挡，不计为 FAILED；FAILED 按 detection/association/control/timeout 分类。
- 执行器：命令延迟 `actuator_delay_frames`，最大速度 `actuator_max_speed`；过载时记录 `saturated_frames`。

## 当前结果（2026-11-07）

当前 11 个场景：SUCCESS 9、FAILED 1、UNKNOWN 1，成功率 0.818；全部终止，无重试。

| 场景 | 结果 | 原因 | 稳定(s) | MAE(px) | 遮挡恢复(s) | IDSW |
|---|---|---|---|---|---|---|
| constant | SUCCESS | none | 0.44 | 70.85 | -1 | 0 |
| stop | SUCCESS | none | 2.56 | 15.70 | -1 | 0 |
| turn | SUCCESS | none | 3.76 | 9.32 | -1 | 1 |
| squat | SUCCESS | none | 2.96 | 8.75 | -1 | 0 |
| occlusion | SUCCESS | none | 1.36 | 26.06 | 0 | 0 |
| camera_pan | SUCCESS | none | 1.36 | 8.72 | -1 | 0 |
| small | SUCCESS | none | 0.48 | 68.42 | -1 | 0 |
| large | SUCCESS | none | 0.48 | 68.32 | -1 | 0 |
| false_positive | SUCCESS | none | 0.48 | 69.24 | -1 | 0 |
| unknown | UNKNOWN | detection | -1 | 0 | -1 | 0 |
| control_fail | FAILED | control | -1 | 227.46 | -1 | 0 |

`constant/small/large/false_positive` 的 MAE 包含初始大误差，不能当作稳态误差；稳态看 `settling_s` 和逐帧 error。

## 限制

- 这是二维合成场景，不是实景 MOT 准确率；不能替代真实录像/身份标注评估。
- 观测由脚本生成，不含真实相机噪声、光照、运动模糊。
- 执行器是简化速度模型，不含真实机械/云台动力学。
- 不接游戏、不注入键鼠，符合总设计 27 章范围。

## 证据

`artifacts/sim/sim-01/runs.jsonl`、`summary.json`、`report.md`（artifacts 被 Git 忽略）；T12 已接入 `tools/eval_report.py`。
