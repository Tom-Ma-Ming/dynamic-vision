# 验收表

| 输入/动作 | 可观察结果 |
|---|---|
| 默认 tracker.yml（byte_track 缺省） | 旧 CTest、1000 帧 blocking 稳定字段不变 |
| byte_track=1，只有 low 观测 | 不创建轨迹 |
| byte_track=1，Tentative 后仅 low 观测 | Tentative 被删除，不被 low 确认 |
| byte_track=1，Confirmed 后 high 缺失、low 在门控内 | 轨迹保持 Confirmed，lastSeen 更新 |
| byte_track=1 回放 | 检测阈值降到 low_score，低置信度框进入跟踪器 |
| SIM-01 low_conf 基线/ByteTrack | 状态不退化；记录 MAE/稳定时间差异 |
| windows-cpu / windows-full | 21/21 不退化 |

实测（2026-11-07）：low 不 birth、不确认 Tentative、Confirmed 可二次关联三项单元断言通过；SIM-01 12 场景基线/ByteTrack 均 SUCCESS 10 / FAILED 1 / UNKNOWN 1；low_conf MAE 68.73→65.44，settle 0.48→0.64（权衡，默认仍关闭）。证据 artifacts/sim/sim-02-base、sim-02-byte。
