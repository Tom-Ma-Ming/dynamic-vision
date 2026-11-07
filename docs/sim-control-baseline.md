# CV 控制与首次选择基线完善 · 2026-11-07

## 已实现

- 新场景配置 configs/sim-control.yml 显式启用 acquire_single_confirmed 与 compensate_actuator_delay。仅在尚未选中过对象且恰有一条无歧义 Confirmed 轨迹时首次选择；选中后沿用持久 ID，删除后不会自动换人。
- 控制命令根据 actuator_delay_frames × dt_s 外推。command_horizon_s 继续作为独立未来预测评分时域；默认旧配置仍可用于消融。
- 执行器在延迟未到之前保持当前位置，修复启动阶段提前执行命令。
- 无可用观测、Lost 或身份歧义时产生保持位置命令。已有执行延迟队列按既定顺序执行。
- 成功阈值 epsilon_enter/exit 与完整场景长度保持原设定。
- 新的实验 tracker-cv-maneuver.yml 仅将 acceleration_sigma 从 180 调为 900，用于机动响应消融。

## 验收

CPU CTest 23/23，pytest 11/11。新增回归覆盖无噪声匀速下延迟补偿误差减半、命令到期前保持、低置信度初始确认失败后等待有效对象、选中后禁止跳人。默认和机动参数的 1/2/5/10 人交叉测试均通过。tools/verify_sim_metrics.py 独立核对三组固定窗口评分与观测相同。

保持原种子，旧控制配置 SUCCESS/FAILED/UNKNOWN 为 0/8/4，新控制 CV 为 2/6/4，ByteTrack 为 2/6/4。ByteTrack 低置信度场景可选中真实存在的确认 ID，覆盖率为 95.7%。这仍属于合成闭环实验，未覆盖真实视频身份真值。

## 三种子过程噪声消融

所有方法使用同一场景、噪声和观测；seed 偏移 0、100、200。统计是三个完整场景窗口的均值。

| 场景 | CV 控制 MAE px | 机动 CV MAE px | CV 覆盖率 | 机动 CV 覆盖率 |
|---|---:|---:|---:|---:|
| 匀速 | 14.525 | 11.420 | 98.3% | 99.2% |
| 急停 | 12.566 | 11.150 | 81.4% | 99.2% |
| 变向 | 95.652 | 8.447 | 58.8% | 99.3% |
| 小目标 | 9.173 | 9.883 | 99.2% | 99.2% |
| 低置信度 | 9.343 | 10.674 | 99.0% | 99.0% |

较高过程噪声改善机动后关联，但增加平稳小目标噪声。实验参数尚未作为默认参数。下一步 IMM 消融应使用相同完整序列，分别看机动事件后窗口、平稳窗口、预测误差和轨迹覆盖率，同时保留单模型两种噪声基线。

## 复现与产物

按 docs/image-replay.md 设置本机 OpenCV PATH 后运行，输出目录必须是新目录。

~~~powershell
out/windows-cpu/apps/sim_runner/Release/sim_runner.exe configs/sim-control.yml artifacts/sim/control-new configs/tracker.yml
.\.venv\Scripts\python tools/ablate_sim_control.py out/windows-cpu/apps/sim_runner/Release/sim_runner.exe configs/sim-control.yml configs/tracker.yml configs/tracker-cv-maneuver.yml artifacts/sim/control-new-seeds
~~~

实测证据位于 artifacts/sim/control-v1-baseline、control-v1-cv、control-v1-byte、control-v1-maneuver 和 control-ablation-seeds/comparison.json。
