# 验收表

| 输入/动作 | 可观察结果 |
|---|---|
| 匀速场景 | result=SUCCESS，稳定时间有限，MAE/P95 有值 |
| 无观测/长遮挡超过 grace | result=UNKNOWN，不能记为 FAILED |
| 执行器速度极小/超时 | result=FAILED，cause=Control/Timeout，不无限重试 |
| 短遮挡 | 恢复后 SUCCESS，occlusion_recovery_s >= 0 |
| 蹲下/变向/相机平移 | 轨迹持续、误差可观测；结果和指标写入 JSON |
| 同一 seed 重复运行 | 状态、终止帧、指标完全一致 |
| runs.jsonl | 每帧有 truth/observed/track/est/pred/aim/error/age |
| summary.json | 按 motion/尺寸桶聚合，含 SUCCESS/FAILED/UNKNOWN 计数 |
| sim_closed_loop CTest | 退出 0；T12 三类结果均出现 |
| windows-cpu / windows-full | 原 20/20 变为 21/21，不退化 |

实测（2026-11-07）：12 场景 SUCCESS 10 / FAILED 1 / UNKNOWN 1，成功率 0.833，全部终止；control_fail 为 FAILED/control，unknown 为 UNKNOWN/detection，occlusion 恢复后 SUCCESS；同一 seed 重跑结果、终止帧、MAE、IDSW 一致。sim_closed_loop 通过；windows-cpu 21/21。证据 artifacts/sim/sim-01/ 与 docs/sim-report.md。
