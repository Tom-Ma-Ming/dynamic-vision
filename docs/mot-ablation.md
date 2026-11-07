> 2026-11-07 修订：以下旧成功率和 MAE 消融结论已撤回。固定窗口、持久选择 ID 和目标尺寸归一化后的结果与复现命令见 docs/review-fixes.md。

# MOT-08 低置信度二次关联（ByteTrack 风格）

## 开关

默认 `configs/tracker.yml` 不包含新键，`byte_track=false`，行为与原来一致。实验配置：

~~~yaml
byte_track: 1
high_score: 0.4
low_score: 0.1
~~~

`vision_replay` 在 `byte_track=1` 时把检测置信度阈值降到 `low_score`，否则保持 0.25；NMS IoU 仍为 0.45。回放第 4 个位置参数可传 `configs/tracker-bytetrack.yml`。

## 关联规则

1. head/person 先合并；
2. 分数 >= high_score 的观测走第一轮全局关联；
3. 未匹配的 Confirmed/Lost 轨迹与 low_score..high_score 的观测做第二轮；
4. low 观测不创建新轨迹，也不能把 Tentative 确认为 Confirmed；
5. 默认关闭时所有观测走原第一轮逻辑。

## 单元测试

`tests/unit/test_tracker.cpp` 新增：

- 低置信度不能确认 Tentative：基准会确认，ByteTrack 删除；
- 低置信度不能 birth；
- 已 Confirmed 轨迹在 high 缺失时可由门控内 low 观测保持 Confirmed，并更新 lastSeen。

`mot_test` 通过；windows-cpu 21/21。

## SIM-01 消融

12 个场景，基线 `configs/tracker.yml` 与 `configs/tracker-bytetrack.yml`：

| 指标 | 基线 | ByteTrack |
|---|---|---|
| SUCCESS / FAILED / UNKNOWN | 10 / 1 / 1 | 10 / 1 / 1 |
| 成功率 | 0.833 | 0.833 |
| low_conf MAE(px) | 68.73 | 65.44 |
| low_conf 稳定(s) | 0.48 | 0.64 |
| control_fail | FAILED/control | FAILED/control |
| unknown | UNKNOWN/detection | UNKNOWN/detection |

低置信度场景 MAE 改善约 3.3px，但稳定时间变长 0.16s；其余场景指标基本一致。这是权衡，不是普遍精度提升，因此默认关闭。

## 证据

`artifacts/sim/sim-02-base/`、`artifacts/sim/sim-02-byte/`（artifacts 被 Git 忽略）。

## 仍待

IMM、镜头补偿、ReID 未实现；真实录像上的消融仍缺身份真值数据。
