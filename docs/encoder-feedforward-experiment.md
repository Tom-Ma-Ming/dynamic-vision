# 编码器外推与速度前馈 · 2026-11-08

新增encoder_extrapolation与velocity_feedforward两个默认关闭开关。因果/饱和契约见openspec/changes/encoder-prediction。全套CPU34/34通过，后补充过期/大间隔及全丢开关反例并定向复测。

## 匀速消融

configs/sim-encoder-feedforward-ablation.yml：120帧避免边界反向，Kp2、延迟80ms、噪声2px、种子11/29，速度20/80；原门槛不变。下表是两种子末40帧平均误差的范围（px），不代表所有帧硬上界。

| 模式 | 20px/s | 80px/s |
|---|---|---|
| 原100ms提前量 | 6.53～6.68 | 25.68～25.90 |
| 仅提前量归零 | 8.63～8.65 | 33.76～33.90 |
| 归零+编码器外推 | 10.22～10.30 | 40.21～40.23 |
| 归零+速度前馈 | 1.44～2.12 | 6.09～6.53 |
| 归零+两者组合 | 0.83～1.35 | 0.84～1.33 |

组合降低匀速偏差，但最大尾段误差仍约2.6～3px，20项合计仅4项满足原任务成功窗口，不能宣称全部验收通过。

## 原动态集合复测

configs/sim-encoder-feedforward-dynamic.yml保留原24项场景，仅开启两项开关和horizon0：5SUCCESS/15FAILED/4UNKNOWN，原配置为3/17/4。4140命令生命周期审计通过，五项标准产物双跑逐字节一致。

这个成功数量不足以证明改善：结果一旦达到窗口就锁存SUCCESS，后面仍可能断轨。Kp2的constant分组末段平均误差仍69.85px（原67.58），turn140.40px（原139.41）；Kp8 turn167.22px（原125.80）。因此只确认了不反向匀速下的改善，动态身份保持和后期误差没有解决。默认继续关闭。

证据artifacts/sim/encoder-feedforward-ablation-v1、encoder-feedforward-dynamic-v1及encoder-feedforward-dynamic-repeat-v1；后者verification.json有分组误差和二进制摘要。下一步首先区分“曾经成功”和“结束时仍稳定”的验收口径，再处理变向关联和全链路异步时钟，不以锁存成功掩盖后续失败。
