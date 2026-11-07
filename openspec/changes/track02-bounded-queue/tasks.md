# 执行任务

- [ ] Q01 在 include/vision 实现有界队列和关闭协议；注册独立 queue_test。
- [ ] Q02 tests/unit 验证容量 1/2、最旧替换、关闭唤醒、生产消费异常和帧数守恒。
- [ ] Q03 apps/trt_infer/replay.cpp 增加显式实时节奏模式，默认顺序回放兼容。
- [ ] Q04 输出延迟分位数和丢帧计数，tools/verify_replay.py 保留语义回归。
- [ ] Q05 CPU CTest、pytest、同步 1000 帧重复回放与 GPU 慢消费者集成验证；记录 artifacts/track02 下实测结果。
- [ ] Q06 更新 docs/progress.md、docs/problems.md、docs/decisions.md。
