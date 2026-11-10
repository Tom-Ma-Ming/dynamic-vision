# 验收协议（待执行）

输入、场景与可观察结果以 docs/human-led-assist-handoff.md 的验收表为准。冻结场景、参数、评分版本和程序摘要后执行；不能按结果调整资格窗口或删除失败帧。

硬门槛：辅助区外无新辅助命令；获知人工接管后无新辅助命令；合法区内上下/斜向场景均有有效对应方向修正；输出不超过声明上限；取消有账目；不自动换人；不读隐藏执行真值；不出现时钟倒退或有界状态泄漏。数值效果门槛须在运行前写入实验配置，并注明尚待体验确认。

合成夹具覆盖独立人工大转向、目标进出、边界抖动、停手恢复、纵向/斜向、丢失、多人、延迟和丢反馈。先以同源输入比较辅助开关，再在渲染→TensorRT 链路复验。真实录像可验证检测/跟踪，但没有人工输入记录时不能单独证明人工接管正确。

HA05 命令：`sim_runner configs/sim-human-led-assist.yml artifacts/human-led-assist/<版本>/HA05 configs/tracker.yml`，随后 `python tools/audit_human_led_assist.py <该输出目录>`。HA06 命令：`python tools/run_ha06.py <sim_runner> configs/tracker.yml artifacts/human-led-assist/<版本>/HA06`。本批 Linux 运行的退出码和数字见 docs/ha07-batch-review.md。Windows `sim_runner.exe` / SDK 本批未重跑。TensorRT 图像腿未执行，报告写 UNKNOWN。

报告必须分开给出资格时间、辅助覆盖、中心误差 P50/P95、稳定性、抖动/超调、接管停止延迟、残余位移、越界输出和身份误切换。资格由独立评分器定义，不允许控制器自行缩短分母。旧协议的失败数据保留，旧动态集合不替代新的人主导验收。
