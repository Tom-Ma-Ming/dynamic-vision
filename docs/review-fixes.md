# 评审修复验收 · 2026-11-07

本轮修复 docs/deepseek-code-review.md 中五项问题。控制器仅按配置 selected_track_id 关注轨迹，默认 1；尚未确认时尝试相同 ID，失效后保留该 ID。真值用于评分。头身歧义检查覆盖高、低两阶段。MAE 计算覆盖完整场景，任务首次 SUCCESS/FAILED/UNKNOWN 事件保持独立。

## 实测结果

| 检查 | 结果 |
|---|---|
| CPU Release CTest | 23/23 |
| SDK Release CTest | 3/3 |
| Python pytest | 11/11 |
| 重合关联回归 | 默认、高分一阶段、低分二阶段均标记身份歧义，Selection 拒绝歧义轨迹 |
| 固定场景独立计算 | 全帧控制 MAE、估计误差、未来真值插值后的预测误差及覆盖率匹配 |
| 真实 Engine 异常关闭 | 两帧间隔 60 秒，注入 overlay 输出路径冲突，0.390 秒退出，summary 明确 error |
| 同步回放重复运行 | 两次各 1000 帧，稳定字段哈希一致，输出视频完整解码 |

## 当前仿真结论

保持原场景参数，CV 基线 SUCCESS/FAILED/UNKNOWN 为 0/8/4，ByteTrack 为 0/7/5。严格的目标尺寸归一化与持久 ID 选择暴露出阈值、控制器和重捕获问题。旧版 10/1/1 与低置信度 MAE 改善结论已撤回。

low_conf 的 140 帧固定窗口中，CV 控制 MAE 为 9.642 px、估计 MAE 1.480 px、0.1 秒预测 MAE 2.177 px、轨迹覆盖率 99.29%。ByteTrack 下所配置 ID 1 尚未确认即被移除，覆盖率 0、控制 MAE 113.463 px，估计/预测 MAE 输出 null。该实验显示目前选择契约下无法建立关注对象；后续应明确手动选中时刻再做进一步消融。

schema_version=2，evaluation_window=full_scenario；mae_px 为兼容保留的 control_mae_px 别名。估计/预测误差仅在轨迹存在的样本计算，同时报告 tracked_frames、prediction_samples 和覆盖率。成功不提前截断场景，首次任务事件后继续评分。未来位置使用相邻真值帧线性插值。预测末尾不足时域的样本排除并明确分母。

## 复现

先按 docs/image-replay.md 配置本机 PATH。在项目根目录运行，输出目录要求尚未存在。

~~~powershell
cmake --build --preset windows-cpu
ctest --preset windows-cpu --output-on-failure
cmake --build --preset windows-sdk
ctest --preset windows-sdk --output-on-failure
.\.venv\Scripts\python -m pytest tests/tools -q
out/windows-cpu/apps/sim_runner/Release/sim_runner.exe configs/sim.yml artifacts/sim/new-base configs/tracker.yml
out/windows-cpu/apps/sim_runner/Release/sim_runner.exe configs/sim.yml artifacts/sim/new-byte configs/tracker-bytetrack.yml
.\.venv\Scripts\python tools/verify_sim_metrics.py artifacts/sim/new-base artifacts/sim/new-byte
.\.venv\Scripts\python tools/verify_replay_shutdown.py out/windows-sdk/apps/trt_infer/Release/vision_replay.exe artifacts/engines/yolo11n-v002-fp32/best-fp32.engine artifacts/code-review/new-shutdown.json
~~~

正式验收产物为 artifacts/sim/review-fixed-v2-base、review-fixed-v2-byte、artifacts/replay/review-fixed 和 artifacts/code-review/shutdown-verification.json。工具 eval_report.py 新增 --sim-summary，避免聚合时误选历史仿真。更新报告位于 artifacts/eval-report-review-fixed，使用上述 v2 base，gates 为 6 pass / 4 partial / 2 missing。CPU CI 的仿真目标 /utf-8 参数已限制为 MSVC；托管 CI 本轮未运行。
