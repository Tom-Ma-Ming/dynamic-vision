# HA07 复审包 · 2026-11-09

这是交给独立复审的材料。复审本身还没有做。ReID、person 召回 95% 和远程 CI 按原计划继续后置，不在本批。

## 产品结论

已选、已确认、不歧义、观测未过期的目标进入辅助区后，控制器沿误差方向步进，并在同一步的实验上限内补上轨迹的图像速度，使慢速目标留在中心附近。区外、人工接管、大转向、歧义、反馈故障、断连和编码器不可用都不发新辅助，并清掉累积速度。人的镜头输入单独记账。

这不是全画面自动追人，也不是已经训练好的镜头策略。

## 实验参数（未确认）

| 键 | 本批值 | 单位 | 在哪里改 |
|---|---:|---|---|
| `assist_enter_px` | 48 | 控制画面 px | `assist_defaults` 或单场景 |
| `assist_exit_px` | 72 | px，必须不小于进入 | 同上 |
| `assist_deadzone_px` | 1 | px，必须小于进入 | 同上 |
| `assist_max_step_px` | 2 | px/次 | 同上 |
| `assist_max_speed_px_s` | 50 | px/s | 同上 |
| `assist_max_accel_px_s2` | 500 | px/s² | 同上 |
| `human_turn_px` | 12 | px，达到则让步 | 同上 |
| `assist_resume_wait_s` | 0.2 | s | 同上 |
| `assist_enabled` | 1 或 0 | 开关 | 关时仍执行同一人工轨迹 |

摘要字段 `parameters_experimental` 为 true。这些数字不能写成产品验收标准。

## 改动位置

- `include/vision/sim/Simulator.hpp`、`src/sim/simulator.cpp`：人主导微调、速度补偿、`assist_enabled`、第二个人、反馈故障停输出。
- `apps/sim_runner/main.cpp`：读取 `assist_defaults`，像素检测闭环。
- `apps/trt_infer/sim_image_main.cpp`：末参 `legacy` / `human_led` / `human_led_off`。本批未编译、未运行。
- `configs/sim-human-led-compare.yml`、`sim-human-led-multi.yml`、`sim-human-led-faults.yml`、`sim-human-led-image.yml`。
- `tools/run_ha06.py`、`tools/audit_human_led_assist.py`、`tests/unit/test_sim.cpp`、`tests/tools/test_run_ha06.py`。

## 本批实际运行

环境：Linux，g++ 13，OpenCV 4.6.0。没有 Windows 预设，没有 TensorRT，没有 Engine。

| 命令 | 退出码 | 结果 |
|---|---:|---|
| `out/linux-cpu/sim_test configs/tracker.yml` | 0 | 打印场景通过，含开/关、慢走、双人、像素闭环和反馈故障停输出 |
| `python3 -m pytest tests/tools/test_run_ha06.py tests/tools/test_audit_human_led_assist.py -q` | 0 | 9 passed |
| HA05 `sim_runner` + `audit_human_led_assist.py` | 0 | `human-led-audit-v1` passed |
| `python3 tools/run_ha06.py ... HA06` | 0 | `ha06-v1` passed；TensorRT `not_run` / UNKNOWN |

HA05 七个场景的旧锁存都是 FAILED。审计不看那个锁存。需求帧 206/206 被辅助，区内末误差：横向 1 px，纵向 1 px，斜向 1.41 px。区外和进入/退出之间辅助为 0。接管期间没有新命令。稳定比例只统计死区以内的合格帧，横向约 0.27，因为大部分帧还在走过去；这不是“始终贴死区”的产品指标。

HA06 数字见 [ha06-experiments.md](ha06-experiments.md)。机器可读快照：

- `docs/evidence/ha06-ha07-20261109/ha05-audit.json`
- `docs/evidence/ha06-ha07-20261109/ha06-report.json`

逐帧 `runs.jsonl` 在 `artifacts/human-led-assist/ha06-ha07-20261109/`，按仓库约定不入库，用上面的命令可以重建。

## 保留的失败和未知

- 旧 epsilon 锁存在 1 px 中心误差上仍是 FAILED。人主导分数看中心误差和审计门，不改旧检查器。
- `dispatch-fail`、`feedback-drop` 是 UNKNOWN。
- `delay-0` 与 `delay-80` 在 60 帧末都是 1 px。这只是本实验步长的观察，不把延迟标成无影响。
- TensorRT 图像闭环没有执行。像素闭环明确标成 `cpu_pixel_closed_loop_not_yolo`。

## 复审时建议看的点

1. 开和关的 `human_dx_px` 序列是否一致，关的辅助是否全程为 0。
2. 交叉重叠帧的 `control_gate=ambiguous` 是否还有辅助步长。
3. 反馈故障帧的 `assist_dx_px` 是否为 0，以及 UNKNOWN 有没有被改成通过。
4. 实验参数有没有被写成已确认的产品标准。
5. 有 Engine 之后，用文档里的 `human_led` / `human_led_off` 命令补跑，不要用本次 UNKNOWN 代替。
