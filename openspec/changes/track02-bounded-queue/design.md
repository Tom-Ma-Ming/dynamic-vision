# 设计约束

CPU 队列置于 include/vision 与 tests/unit，独立使用 C++17 标准库和 Threads::Threads。根 CMake 在任何 PROBE_ONLY return 之前注册测试；CI 的 CPU job 必须运行。

FramePacket 拥有图像数据、frame_id、source_ts_ns、capture_steady_ns。录像运动用 source_ts_ns，延迟只在 steady_clock 内相减。生产者按媒体时间节奏入队；消费者独占 Detector 和 Tracker，输出不可变快照。容量为 1 或 2，可配置。满时弹出最旧待处理帧；正在推理的帧不被修改。

关闭状态禁止继续入队，唤醒等待消费者；正常结束排空队列。异常记录 exception_ptr，关闭队列并 join 线程后向主线程报告，禁止线程析构时 terminate。任何输出异常也触发相同关闭路径。

输出包含捕获数、成功处理数、被替换数、失败数、队列峰值及 queue_wait_ms/infer_ms/result_age_ms 的 P50/P95/P99。测试结束按已入队帧核对守恒，失败路径亦给出分项计数。保留旧检测、轨迹 JSON 字段。回归仅比较确定性字段，实时丢帧结果使用不变量验收。

回退通过顺序模式，测试合成时钟/慢消费者无需 GPU。OpenCV/TRT 实时回放为集成验证层。
