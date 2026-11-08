# 编码器延迟的时钟分解与持续稳定口径 · 2026-11-08

阈值、默认 Kp=8、默认跟踪器和选择策略都没有改。没有新增控制开关。本轮只把“曾经进入稳定窗口”和“结束时仍稳定”分开，并在每帧记下观测年龄、已接收编码器年龄、正向命令飞行时间和滤波器速度。控制计算不读取真值位置，也不读取尚未送达的编码器累计位移。

## 坐标和时钟

场景步长 40ms。编码器延迟 80ms、执行器延迟 2 帧时，外推加前馈、horizon 为 0 的无噪声 80px/s 场景在可控帧上测得：

| 时钟 | 含义 | 测得 |
|---|---|---|
| 观测年龄 | 当前仿真时刻减最近已发布观测的采集时刻 | 80ms |
| 编码器年龄 | 当前仿真时刻减最近已送达编码器样本的采集时刻 | 80ms |
| 命令飞行 | 本帧命令预定到达时刻减当前仿真时刻 | 80ms |

已送达样本的采集时刻不晚于当前仿真时刻。编码器全部丢失时，外推和前馈仍然不生成命令。

控制用的图像位置是：把轨迹预测到当前仿真时刻，再减去“当前可用编码器减去该观测配对编码器”。可用编码器只取已送达样本；外推打开时只用最后两条已送达样本，按采样间隔求速度，外推到当前时刻，速度模长不超过执行器上限。命令为

```
u = Kp * (x_image - aim) * units + feedforward * v_filter * units
```

`feedforward` 为 0 或 1。无噪声 80px/s 对准之后，`v_filter` 仍约为 +80px/s，而图像位置已经靠近瞄准点。这个速度是编码器补偿所抵消的目标速率，不是补偿后的图像残差速率。因此前馈提供维持跟随所需的速率，比例项只修位置。

只有比例项且 horizon 为 0 时，20px/s 最后 40 帧平均有符号误差为 −8.4px，80px/s 为 −33.6px。这与 `v/Kp - v*0.08s` 接近，分别是 10−1.6 和 40−6.4。默认 100ms 提前量会抵消其中一部分，所以不能把提前量归零当成单独的改进。

把图像位置再提前一个命令飞行时间，并减去已经发出、尚未执行的位移，在匀速稳态里两项相消，结果和原来相同。若打开前馈后只减去未执行位移、不再加飞行时间，急停和变向的尾段误差变大。这项校正没有做成开关，也没有进入默认控制。

## 结果口径

`SUCCESS` 仍是首次凑满 `settle_frames` 的锁存，后面的断轨或超差不会把它改回 `FAILED`。另外两个指标不替代它：

- `success_maintained`：锁存之后还至少观察到 `settle_frames` 帧，且这些帧全部可控，归一化误差不超过 `epsilon_exit`。
- `end_stable`：全场景最后连续 `settle_frames` 帧都可控，且归一化误差严格小于 `epsilon_enter`。

`summary.json` 现为 schema 3。更早的 schema 2 摘要没有这些字段，不能当作持续稳定证据。`tools/audit_sim_stability.py` 用完整 `runs.jsonl` 重算这两个指标。

归一化误差是 `hypot(dx/width, dy/height)`。默认人物框 24×48、`epsilon_enter=0.02` 时，纯水平误差必须小于 0.48px。

## 原场景复跑

`configs/sim-encoder-transport.yml` 仍是 0 SUCCESS / 2 FAILED / 2 UNKNOWN，持续稳定和结束稳定都是 0。这不是整组通过。

`configs/sim-encoder-feedforward-dynamic.yml` 仍是 5 SUCCESS / 15 FAILED / 4 UNKNOWN。5 次锁存之后都很快失效，持续稳定和结束稳定都是 0：

| 场景 | 锁存帧 | 首次随后违规帧 | 违规帧数 |
|---|---:|---:|---:|
| kp2-constant-d80-s29 | 127 | 132 | 68 |
| kp2-constant-d120-s29 | 131 | 132 | 68 |
| kp2-stop-d80-s11 | 170 | 171 | 14 |
| kp2-stop-d120-s11 | 169 | 171 | 16 |
| kp2-turn-d80-s29 | 156 | 157 | 33 |

4140 条命令都有且只有一次终结。`runs.jsonl`、`commands.jsonl`、`summary.json`、`feedback.jsonl`、`report.md` 两次运行逐字节相同。按运动分组，匀速和变向各有 4 项在后段出现 `no_selected_track`；急停没有丢所选 ID，但末 40 帧平均误差仍约 31px。选择逻辑仍然不会在旧 ID 消失后自动改选另一个 ID。

同一 24 项改用 `configs/tracker-cv-maneuver.yml`（加速度标准差 900）后，24 项都没有丢掉所选 ID，但也没有任何一项锁存成功或结束稳定，三个运动分组的末段平均误差都在 30px 左右。更高过程噪声避免了断轨，同时让严格窗口更难成立。默认跟踪器不改。

无噪声、不碰边界的 80px/s 用外推、前馈和 horizon 0，可以在原门槛下结束稳定，末段平均误差约 0.25px。加上标准差 2px 的检测噪声后，估计相对瞄准点、估计相对真值都在 1px 量级，大于 0.48px 的进入门槛。这个残差来自已发布检测，不能靠把真值送进控制器消掉。

## 尚未完成

动态 24 项没有一项结束稳定，主任务不能标成完成。固定步长里的服务耗时仍是仿真模型，实际异步调度没有开始。故障禁发、旧 epoch/nonce、停止屏障和命令一次终结这几项由原命令审计覆盖，本轮没有放宽。

证据目录：`artifacts/sim/encoder-transport-clock-v1`、`encoder-feedforward-stable-v1` 和 `encoder-feedforward-stable-repeat-v1`。机动对照在 `encoder-feedforward-maneuver-stable-v1`。这些是合成检测，不是 TensorRT 图像或真实录像精度。

```powershell
$env:PATH = "F:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;F:\opencv\opencv\build\x64\vc16\bin;" + $env:PATH
cmake --build --preset windows-cpu --target sim_runner
./out/windows-cpu/apps/sim_runner/Release/sim_runner.exe configs/sim-encoder-transport.yml artifacts/sim/encoder-transport-clock-v1 configs/tracker.yml
./out/windows-cpu/apps/sim_runner/Release/sim_runner.exe configs/sim-encoder-feedforward-dynamic.yml artifacts/sim/encoder-feedforward-stable-v1 configs/tracker.yml
$env:PYTHONUTF8='1'
./.venv/Scripts/python tools/audit_sim_commands.py artifacts/sim/encoder-feedforward-stable-v1/commands.jsonl
./.venv/Scripts/python tools/audit_sim_stability.py artifacts/sim/encoder-feedforward-stable-v1
```
