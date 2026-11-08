# 仿真执行故障与恢复 · 2026-11-08

新增默认关闭的 disconnect_start/disconnect_frames 和 dispatch_failure_rate，仅用于 incremental_control。断连按帧边界生效：此前时间段已经执行的动作保留，边界处取消活动与在途命令、清量化余数和PID状态；断连期间不生成新命令，恢复后从当前观测重新计算，不重放旧代命令。

发送失败使用独立固定种子随机流：generated 后记录 dispatch_failed，失败请求不进入传输队列，账目清理且PID重置。此前已经接受的有效命令不因一次新发送失败被假定未执行。control_gate 区分 actuator_disconnected/dispatch_failed；连续失败导致UNKNOWN时原因是control，不能冒充检测失败。

CPU CTest27/27。configs/sim-faults.yml 三组双跑输出完全一致：短断连恢复SUCCESS、20%间歇发送失败SUCCESS、全部发送失败UNKNOWN/control。artifacts/sim/fault-recovery-v2 和 fault-recovery-repeat-v2 保存证据，verification.json 检查每条生成命令恰有一个终结/窗口结束状态，恢复后的接受命令不来自断连前，全部发送失败没有应用事件。

审计同时发现并修复场景加载器将double步长经float读取的问题，默认40ms此前得到39999999ns；现使用double读取时间及双精度控制配置，逐帧验证ts_ns=frame×40000000。v1运行保留为问题证据，不作为最终时间核验。

测试覆盖断连窗口位置不变化、不生成命令、恢复后静止目标误差<3px、全部发送失败不移动及正确归因。未改变既有精度阈值或默认参数。这是确定性执行故障模型，不包含真实设备接入、反馈消息丢失、设备固件缓存或真实连接恢复。完整系统长时故障验收仍待完成。

复现：按 image-replay.md 设置OpenCV PATH并构建 windows-cpu，执行 sim_runner configs/sim-faults.yml 新输出目录 configs/tracker.yml。

可复用命令账目审计（仅检查事件契约，不代表控制精度通过）：

```powershell
.\.venv\Scripts\python tools/audit_sim_commands.py artifacts/sim/fault-recovery-v2/commands.jsonl --repeat artifacts/sim/fault-recovery-repeat-v2/commands.jsonl
```

工具按 scene/id 隔离命令，拒绝缺失生成、重复生成、接受前执行、失败后继续执行、不匹配终结和未终结命令；11项工具测试包含损坏证据。applied 是时间段聚合报告，不能据其报告时间推导真实设备动作时刻。
