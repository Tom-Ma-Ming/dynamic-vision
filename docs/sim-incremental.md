# 增量执行器阶段证据 · 2026-11-08

incremental_control 默认关闭。新增独立 PID、执行单位/原图像素换算、延迟传输队列和量化执行器。基线绝对目标点模式保留。配置 configs/sim-incremental.yml 为 3 种运动 × 3 seed × 0/20 ms 抖动 × 无/固定延迟补偿，共 36 场景。补偿只使用名义延迟，不读取未来实际抖动。

## 验证

CPU CTest 27/27；两次运行 runs.jsonl、commands.jsonl、summary.json 逐字节一致。证据 artifacts/sim/incremental-v1 与 incremental-repeat-v1，命令分类审计 command-audit.json。

5,776 条命令、22,355 个事件；5,714 accepted、5,089 applied 事件、5,678 replaced、24 cancelled_in_transit、20 cancelled、38 window_end_pending、16 window_end_active。每条生成命令恰有一个终结/窗口结束状态；应用事件均有接受记录且不早于到达。applied 是推进区间累计整数增量，timestamp 是推进/观察时刻，不是硬件逐步执行时刻。

| 场景 | 抖动 ms | 无补偿 MAE px | 名义延迟补偿 MAE px |
|---|---:|---:|---:|
| 匀速 | 0 | 34.807 | 32.282 |
| 匀速 | 20 | 34.464 | 31.735 |
| 急停 | 0 | 8.981 | 7.857 |
| 急停 | 20 | 8.752 | 7.415 |
| 变向 | 0 | 65.409 | 64.092 |
| 变向 | 20 | 65.136 | 63.548 |

为三个种子的全窗口平均，包含启动和失去目标的阶段。严格结果共 2 SUCCESS / 14 FAILED / 20 UNKNOWN；改善不等于通过控制验收。未改变成功阈值。当前使用默认 CV 跟踪配置，需要进一步分解失去轨迹与执行误差。

## 复现

先按 image-replay.md 设置 OpenCV PATH 并构建 windows-cpu，然后运行：

```powershell
out/windows-cpu/apps/sim_runner/Release/sim_runner.exe configs/sim-incremental.yml artifacts/sim/incremental-new configs/tracker.yml
```

命令事件含 scene、kind、id、frame_id、track_id、timestamp、generated、arrival、expires、dx、dy。生成事件记录请求增量；应用事件记录该推进区间应用量。其它事件 dx/dy=0。

## 剩余工作

当前为合成检测与二维光标反馈，尚未驱动视角或渲染并进入 YOLO。动态延迟估计器尚未实现；抖动模型只是测试输入。命令执行为事件区间内的速率限幅/量化近似，未建模设备加速度、写入失败和真实采样延迟。需要补充长延迟/乱序/过期场景、自动审计工具及命令量守恒测试，再推进视觉反馈。不得将这批诊断结果当作真实操作性能验收。
