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

## 2026-11-09 延迟补偿与阶段调度

`encoder_delay_compensation` 默认关闭。只有场景显式写成 1，并且 `encoder_delay_ms>0`、没有 `feedback_prediction` 时，才进入延迟补偿。关闭时跟踪噪声、机动半径、协方差门、前馈和两个轴都保持原配置。摘要里的 `effective` 记下这次实际使用的测量噪声、过程噪声、机动半径、门控协方差来源和两个前馈开关。两个轴都参与控制，不再把垂直命令写成 0，也不再按水平场景把亚像素比例项限幅。速度不超过 5px/s 时，位置误差减去已经发出、尚未执行的位移，预测时域用反馈延迟估计。速度更高且没有显式 `velocity_feedforward` 时，时域为 0，图像位置用滤波速度乘编码器延迟补到当前时刻，命令是 `v_filter*units + Kp*e`，`e` 只减去在途命令的反馈分量。

延迟补偿不再改跟踪器的过程噪声、机动半径或恢复协方差，也不再把控制门换成融合时刻的协方差。门控比较的是控制时刻预测位置的方差；若命令额外用了 `v * 编码器延迟`，这个提前量的速度方差也算进去。前馈打开时，在途反馈分量只随已经交付的合法反馈减少。设备已经执行但回传还在延迟或被丢弃时，控制器仍按未交付的发出量计算。默认配置的机动半径仍是 0。半径大于 0 时，只有场上仅有一条已建立轨迹才会接受半径内的唯一检测，更新后的协方差保留，不重置成出生先验。旁边已有另一条轨迹时，新检测不能继承丢失轨迹的 ID。ByteTrack 仍不走这条恢复。

下面两组数字来自补偿默认开启、并且垂直轴被关掉时的运行，不是当前默认。当前默认关闭补偿，原传输配置不会自动套用新控制律。

`configs/sim-encoder-transport.yml` 复跑结果是 2 SUCCESS / 0 FAILED / 2 UNKNOWN。`encoder_delayed` 和 `encoder_recovered` 都是持续稳定且结束稳定，全窗口 MAE 分别是 11.825px 和 33.825px。`encoder_lost` 与 `encoder_stale` 仍是 UNKNOWN，门控 `encoder_unavailable`，命令日志里没有这两项。374 条命令都恰好一次终结。证据在 `artifacts/sim/encoder-transport-delay-v3`。

`configs/sim-encoder-gain-holdout.yml` 现在是 23 SUCCESS / 1 FAILED / 0 UNKNOWN，持续稳定 8，结束稳定 15，身份切换 0。门槛没改。结束稳定的包括两组停止、以及多组 Kp=2/Kp=8 的 80ms 匀速和变向，末 5 帧误差在 0 到 0.4px。没有结束稳定的 8 项，末 5 帧里有一帧是 0.6 或 0.8px：80px/s 每帧 3.2px，执行器只走整数像素，余数积到 1px 时多走一步，误差按大约 0.2px 的台阶来回。速度估计差 0.5px/s 就会把这一步的相位挪一帧，末段因此含 0.6px。唯一失败的是 `kp8-constant-d120-s11`，末 5 帧是 0.8、1.0、2.2、2.4、2.6px，末帧瞄准点离真值 +2.60px，估计离真值 +1.85px，门控仍是 ready。120ms 延迟上这组估计本身就在 0.48px 外面，把瞄准点对准估计也过不了原门槛。

显式前馈的 24 项是 10 SUCCESS / 14 FAILED / 0 UNKNOWN，持续稳定 6，结束稳定 3。这条路径打开了 `velocity_feedforward`，所以上面的垂直保持和亚像素增益上限都不生效，Kp=8 末段仍有 10–60px 的振荡。证据在 `artifacts/sim/encoder-holdout-delay-v2` 和 `encoder-feedforward-delay-v2`。动态集合因此仍未完成。

`async_stages` 默认关闭，原固定步长输出不变。打开后，捕获、推理、反馈、执行进入有界事件队列，同一时刻的顺序是捕获、推理、反馈、执行；推理在对应捕获完成前不能出队；停止事件在同一时刻先于普通命令。推理完成时刻可以落在 40ms 网格之外，发布时刻用这个完成时刻，而不是下一个场景帧。队列满时拒绝新事件并计数，不丢弃已经排队的事件。执行仍由命令到达时刻触发。这不是壁钟线程。无编码器样本时仍然不生成命令。

```powershell
$env:PATH = "F:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;F:\opencv\opencv\build\x64\vc16\bin;" + $env:PATH
cmake --build --preset windows-cpu --target sim_runner
./out/windows-cpu/apps/sim_runner/Release/sim_runner.exe configs/sim-encoder-transport.yml artifacts/sim/encoder-transport-delay-v3 configs/tracker.yml
./out/windows-cpu/apps/sim_runner/Release/sim_runner.exe configs/sim-encoder-gain-holdout.yml artifacts/sim/encoder-holdout-delay-v2 configs/tracker.yml
```


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
