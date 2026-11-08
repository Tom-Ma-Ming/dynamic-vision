# 仿真命令反馈丢失 · 2026-11-08

新增默认关闭的feedback_drop_rate与feedback_timeout_ms。前者以独立固定种子流丢弃accepted/applied/expired/replaced/rejected通知；commands.jsonl仍记录执行器实际事件。因此“没有收到执行反馈”不会被解释为没有移动，亦不会凭实际事件绕过丢包刷新控制器账目。

账目期限从命令生成计时，等待完整生命周期终结，不仅等待接收确认。缺失通知不刷新延迟估计/已应用量/终结账目；pending超时后锁存feedback_timeout、清理控制器账目、停止新命令、取消仿真活动/在途命令并重置PID。反馈超时计入UNKNOWN/control，不伪装成检测漏失。收到一次ack不代表执行完成。不会自动清故障后重发旧命令。

两项参数必须有限；丢失率0～1，超时为0（禁用）或0.001～10000ms；启用丢失要求超时非零，启用超时要求incremental_control。默认参数不变。runs.jsonl新增累计feedback_dropped、控制器feedback_pending和feedback_fault，控制门控为feedback_timeout；summary保存实验参数。

configs/sim-feedback-loss.yml固定三场景双跑结果：

| 场景 | 结果 | 首次故障帧 | 丢失通知 | 最大未终结账目数 |
|---|---|---:|---:|---:|
| 可靠反馈 | SUCCESS | 无 | 0 | 3 |
| 全部反馈丢失，200ms期限 | UNKNOWN/control | 6 | 10 | 5 |
| 20%反馈丢失，500ms期限 | UNKNOWN/control | 19 | 11 | 6 |

证据 artifacts/sim/feedback-loss-v1、feedback-loss-repeat-v1：runs/commands/summary逐字节一致；222条命令各有唯一终结或窗口结束事件。默认回归fault-feedback-regression-v1与此前fault-recovery-v2的命令逐字节一致，排除新增诊断字段后每帧既有字段一致。CPU34/34通过，覆盖实际动作不会因丢反馈消失、超时后不生成/不移动、账目有界、故障锁存及可靠反馈无误触发。

范围限制：这是命令通知丢失模型，未模拟理想encoder位移反馈丢失、反馈传输延迟/乱序、执行器失控或取消失败；本地取消在仿真中可靠。真实设备不能据“发出取消”推断已停止，需要独立确认与设备保护。本轮锁存后没有恢复协议，故障恢复与重新同步仍待实现。20%丢包导致停止是当前保守故障行为，不是鲁棒跟踪成功。

复现：运行sim_runner configs/sim-feedback-loss.yml 新输出目录 configs/tracker.yml；随后使用tools/audit_sim_commands.py核验命令日志。该场景仅使用CPU，不影响正在运行的小时回放程序文件。
