# 验收证据

命令与证据入口见 docs/review-fixes.md。CPU 日志在 artifacts/code-review/fix-ctest.log。等待关闭故障注入报告为 artifacts/code-review/shutdown-verification.json。固定场景结果在 artifacts/sim/review-fixed-v2-base 和 review-fixed-v2-byte；比较脚本为 tools/verify_sim_metrics.py。回放在 artifacts/replay/review-fixed，first/second 两轮各 1000 帧。
这些测试验证实现和评分的一致性。严格阈值下场景成功率仍为零，记录实际结果，后续需算法/控制参数实验。
