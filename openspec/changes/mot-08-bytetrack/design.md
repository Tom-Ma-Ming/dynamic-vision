# 设计

## 配置

`configs/tracker.yml` 保持原键，默认 `byte_track=false`、`high_score=birth_score`、`low_score=0.1`。实验配置 `configs/tracker-bytetrack.yml` 显式打开。`Config::load` 对三个新键使用可选读取，缺省不改变旧配置行为；校验 `high_score >= low_score` 且在 [0,1]。

## 关联流程

1. `group` 先合并 head/person 观测。
2. 按观测分数分为 high（`>= high_score`）和 low（`>= low_score` 且 `< high_score`）。
3. 第一轮：现有全局一对一分配，仅对 high 观测；未匹配的 Tentative 轨迹删除，未匹配的 Confirmed/Lost 轨迹进入第二轮。
4. 第二轮：仅 Confirmed/Lost 轨迹与 low 观测做全局一对一分配；命中则执行同一 Kalman/头身更新，未命中转 Lost。
5. 新轨迹只从 high 未匹配且分数 `>= birth_score` 的观测创建；low 观测永不创建轨迹。

默认 `byte_track=false` 时走原逻辑，所有观测参与第一轮，行为与 TRACK-02/MOT 基线一致。

## 回放阈值

`vision_replay` 加载 tracker 配置后，若 `byte_track=1` 则检测置信度阈值取 `low_score`，否则保持 0.25；NMS IoU 仍为 0.45。这样低置信度框能到达跟踪器，而不是在检测阶段被丢弃。

## 回退

把 `configs/tracker.yml` 的 `byte_track` 保持/改回 0，或使用原 tracker.yml；不需要重编译。第二关联分支只在开关打开时执行。

## 风险

低置信度框可能增加关联机会，也可能让轨迹被噪声拉偏；SIM-01 的 low_conf 场景和单元测试用于观察 MAE/稳定时间，不宣称普遍精度提升。默认关闭，性能退化可直接回退。
