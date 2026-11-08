# 编码器延迟控制参数对照 · 2026-11-08

代码基线dab2ec6，当前默认Kp=8未修改。场景原误差阈值、稳定帧数和窗口不放宽。先用静止无噪声做校准，再固定候选Kp=2，与Kp=8在另设动态场景比较；该动态集合现已被检查，后续优化不能继续称它为未见测试集。

## 校准

configs/sim-encoder-gain-calibration.yml：Kp=2/4/6/8，编码器延迟0/40/80/120ms，共16场景。Kp=2和4均4/4成功、末40帧误差为0；按四延迟中最慢稳定时间选择Kp=2（2.64s，Kp=4为4.32s）。Kp=6成功2/4，Kp=8成功1/4。Kp=8在80ms下末40帧平均误差9.175px、最大17px，在120ms下为83px/125px。此前单独引用最终帧2px不能代表整个末段稳定性。

configs/sim-encoder-low-gain.yml保留原四项传输用例，仅显式设Kp=2：两个静止可控场景SUCCESS，全丢与过期仍UNKNOWN/control。未更改原sim-encoder-transport.yml或全局默认。

## 动态对照

configs/sim-encoder-gain-holdout.yml：两增益×匀速/急停/变向×80/120ms延迟×seed11/29，共24项。每项200帧，vx80、噪声2px、success_after_frame120；误差统计使用最后40帧，纳入丢失跟踪时的真实执行误差。

| 增益 | 场景 | SUCCESS/FAILED/UNKNOWN | 末段平均/最大误差px |
|---|---|---|---|
| 2 | 匀速 | 0/4/0 | 67.58 / 173.61 |
| 2 | 急停 | 3/1/0 | 0.91 / 1.28 |
| 2 | 变向 | 0/2/2 | 139.41 / 321.00 |
| 8 | 匀速 | 0/4/0 | 100.69 / 209.72 |
| 8 | 急停 | 0/4/0 | 54.42 / 125.06 |
| 8 | 变向 | 0/2/2 | 125.80 / 312.27 |

低增益明显改善静止和急停，但匀速/变向仍严重失败，变向平均误差还回退。因此不改变默认增益，也不声称动态延迟已修复。后续必须定位预测/坐标补偿/关联和离开画面的因素，不能只做参数搜索。

证据：artifacts/sim/encoder-gain-calibration-v1、encoder-low-gain-v1、encoder-gain-holdout-v1及encoder-gain-holdout-repeat-v1。动态双跑五项标准产物逐字节一致；4140条命令审计通过。verification.json记录分组结果及程序/配置SHA256。以上为合成检测仿真，不是TensorRT图像/真实录像控制精度证明。
