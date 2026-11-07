# 执行任务

- [x] Q01 在 include/vision 实现有界队列和关闭协议；把 queue 用例注册到 vision_tests。实现：include/vision/core/BoundedQueue.hpp；测试：tests/unit/test_queue.cpp；CPU CTest 通过。
- [x] Q02 tests/unit 验证容量 1/2、最旧替换、关闭唤醒、生产消费异常和帧数守恒。覆盖 LatestCapacityOne/Two、LatestSlowConsumer、BlockingBackpressure、CloseWakes/CloseDrains、ConsumerFailureUnblocksProducer、PushAfterClose、Peak。
- [x] Q03 apps/trt_infer/replay.cpp 增加显式实时节奏模式，默认顺序回放兼容。实现：FramePacket/ReplayConfig、configs/replay.yml 与 replay-realtime.yml；blocking 1000 帧稳定字段与既有证据一致。
- [x] Q04 输出延迟分位数和丢帧计数，tools/verify_replay.py 保留语义回归。summary.json 增加 queue_wait_ms/infer_ms/mot_ms/result_age_ms 与 captured/processed/replaced/rejected/peak；verify_replay 双跑哈希一致。
- [x] Q05 CPU CTest、pytest、同步 1000 帧重复回放与 GPU 慢消费者集成验证；记录 artifacts/track02 下实测结果。CPU 20/20、windows-sdk 3/3、pytest 5/5；realtime 25 FPS 1000/1000/0，500 FPS 过载 1000/250/750；证据 artifacts/track02/verification.md。
- [x] Q06 更新 docs/progress.md、docs/problems.md、docs/decisions.md，补充 docs/image-replay.md 与交接状态。
