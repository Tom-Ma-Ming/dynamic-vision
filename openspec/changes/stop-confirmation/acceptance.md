# 可执行验收

- control_contract：未来报告、错误会话、错误 nonce、非停止状态、期限边界、上一请求回复与重复回复均不得成功；新的有效回复只接受一次。
- sim_closed_loop：前30帧回传全丢、80ms回传、200ms期限时，第32帧才恢复，故障窗口内无新命令；500ms回传超过期限不得恢复。
- configs/sim-stop-confirmation.yml 双跑：完整产物逐字节一致，delayed_stop 恢复；expired_stop、lost_stop 保持故障，不得以放宽阈值改成成功。
- audit_sim_commands.py 对各场景检查命令生命周期，每条命令恰好一个终结事件。
- 完整 CPU CTest 回归通过，保留默认关闭行为既有断言。
