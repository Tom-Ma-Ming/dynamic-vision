# 验收表

| 输入/动作 | 可观察结果 |
|---|---|
| 完整 artifacts 跑默认模式 | 生成 report.json/md/html；检测、Engine、MOT、回放、证据、gates 均有内容 |
| 缺少 Engine 或 MOT 证据 | 对应 section 为 missing；gaps 明确；默认仍返回 0 |
| 缺少必需证据且 --strict | 返回 1，并列出缺失项 |
| 同一输入重复运行 | JSON 的 inputs SHA 和排序稳定；报告内容一致（generated_at 除外） |
| 检测报告 | 逐类指标、conf 0.25 操作点、尺寸桶均来自 person-diagnosis；不填充缺失桶 |
| MOT 报告 | synthetic-metrics 标为合成序列；实景 MOT 标为 missing |
| 回放报告 | blocking/latest 的 capacity、processed/replaced、P50/P95/P99 可对照 |
| 验收矩阵 | T01–T12 每项都有 status 和 evidence；未运行项标 partial/missing |
| HTML | 单文件、无外部依赖、特殊字符转义 |
| pytest | tests/tools/test_eval_report.py 通过；真实 artifacts 的 --strict 返回 0 |

实测（2026-11-07）：真实 artifacts 生成 report.json/md/html；状态 complete-with-gaps，当时 gates 6 pass/3 partial/3 missing（SIM-01 接入后 7/3/2），7 项缺口；pytest tests/tools 8/8（后续新增 benchmark 测试后 10/10）。缺失 fixture 默认返回 0、--strict 返回 1。证据 artifacts/eval-report/。
