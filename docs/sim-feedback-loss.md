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

## 2026-11-08 显式停止确认后的仿真恢复

新增feedback_drop_end_frame（-1为持续注入）和feedback_resync_frame（-1为不请求恢复）。达到请求帧后，仅在故障锁存且连接正常时请求当前停止状态；执行器无活动命令、传输队列为空且活动命令账目已清空，才产生可接受的停止快照。快照经过同一丢包模型，未收到不解除故障。收到后清控制账目/PID并允许按当前观测生成新命令；命令ID继续递增。一次配置只允许一次成功重新同步，后续再次超时不会无限自动解除。

configs/sim-feedback-recovery.yml双跑：反馈在第30帧恢复，显式请求从第20帧开始，11次尝试后第30帧确认停止并恢复，场景SUCCESS；通道恢复但未请求、停止快照始终丢失两场景均UNKNOWN/control。185条命令各有唯一终结/窗口结束状态，恢复后没有接受旧代命令。日志runs/commands/summary逐字节一致；原feedback-loss-v1在新增诊断字段以外的行为完全不变。CPU34/34通过。

证据 artifacts/sim/feedback-recovery-v1、feedback-recovery-repeat-v1、feedback-loss-regression-v2。feedback_resynced表示本场景曾成功同步，feedback_sync_attempts记录累计尝试；当前是否故障仍看feedback_fault。恢复场景显式使用100帧未知宽限和第50帧后的成功窗口，不修改原故障场景门槛。

这补齐了零延迟仿真通道的停止确认恢复模型；仍不是实际设备恢复协议。没有消息乱序、重启epoch、延迟旧快照/重放保护或取消失败模型；这些仍待完整通信层实现。此前“没有恢复协议”描述由本段更新，encoder位移反馈缺失与真实设备验收仍未完成。
