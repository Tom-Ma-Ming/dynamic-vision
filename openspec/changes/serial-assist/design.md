# 设计契约

协议具体化：见 docs/serial-protocol-baseline.md。当前 legacy_m_noack 固件没有应答或停止，禁止等待OK、调用probe_fire或把M 0 0当STOP。用标定发送节奏和有界主机队列，设备执行仅可作视觉估计；ACK匹配条款只适用于可选ack模式。write_completed是主机结果，设备结果可为UNKNOWN，不伪造applied。

详细契约见 docs/serial-assist-implementation-plan.md 的接口、串口、PID与日志章节。控制状态单一所有者，串口I/O独立且有界，观测/意图/传输/执行证据分离；主机 steady_clock ns，图像 px，设备 counts，转换由标定描述。

ACK不能当执行完成。无序号协议限制在途数，迟到应答不得跨epoch；停止能力缺失须报告，已发送不可假撤回。fault不自动恢复。提供sim/mock/dry_run/serial后端；limited_step保留，fixed_pid实验可回退。关闭/异常需有限等待和命令终态日志。
