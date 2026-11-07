# EVAL-01 统一分层评估报告

## 目标

把已经存在的检测、Engine、MOT、回放延迟和 CTest/回归证据汇总成一份可重复、可追溯的分层评估报告，输出 JSON、Markdown 和 HTML。报告只陈述已测数据，缺失的场景分层和指标必须明确标为缺失，不能用默认值填充。

## 问题

当前证据分散在 baseline.json、person-diagnosis.json、engine manifest/verification、synthetic-metrics.json、多个 replay summary 和 artifacts/track02 日志中。人工汇总容易只引用有利数字，也容易把合成序列的 MOT 指标当成实景准确率。

## 范围

- 输入：artifacts 下的现有 JSON、日志和 docs/environment-lock.md。
- 输出：report.json（机器可读）、report.md、report.html（人读）。
- 分层：检测按类别和尺寸桶；MOT 标注合成/实景；延迟按 blocking/latest 和负载；数据按 train/val、负样本、场景元数据。
- 验收矩阵：T01–T12 的当前证据状态（通过/部分/缺失），不伪造未运行项。
- 不重新训练模型、不重新构建 Engine、不修改跟踪算法。

## 后续

真实录像、身份真值、多场景和负样本到位后，把对应输入加入同一报告即可；报告结构不变。
