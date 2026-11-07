# 验收表

| 输入/动作 | 可观察结果 |
|---|---|
| 容量 1，生产 1/2/3 后才消费 | 仅取 3；替换数 2，峰值 1 |
| 容量 2，同上 | 取 2/3，替换数 1，峰值 2 |
| 空队列中等待，另线程关闭 | 消费者有界时间内退出，线程全部 join |
| 未排空时正常关闭 | 剩余帧按序取完，然后返回结束 |
| 消费者抛错或写文件失败 | 关闭并 join，失败明确；无死锁 |
| T07 生产快于消费 | 采集数=处理数+替换数+失败/取消数，峰值≤容量 |
| T08 旧帧晚到 | frame_id 和 timestamp 拒绝过期，轨迹状态保持最新 |
| 同步固定 1000 帧两轮 | 稳定字段逐帧一致，视频可完整解码 |
| 实时节奏录像+慢消费者 | 丢帧>0；队列等待/观测年龄 P50/P95/P99 有效、非负 |

预计入口：cmake --preset windows-cpu -DOpenCV_DIR=F:/opencv/opencv/build，然后 cmake --build --preset windows-cpu 与 ctest --preset windows-cpu --output-on-failure；Python 工具测试为 python -m pytest tests/tools -q。

实测（2026-11-07）：
- CPU CTest 20/20（11 个队列、5 个统计、mot_synthetic、Smoke、2 个 probe）；windows-sdk 3/3；pytest 5/5。
- blocking 1000 帧：1000/1000，replaced=0；稳定字段与 artifacts/replay/mot-v1/first 逐字段一致；verify_replay 双跑哈希一致。
- realtime 25 FPS：1000/1000/0（无丢帧）；500 FPS 过载：captured=1000，processed=250，replaced=750，rejected=0，overlay 250 帧；frame_id/capture/publish 单调，queue_wait 与 result_age 非负，守恒 1000=250+750。
- 详细日志与 summary：artifacts/track02/verification.md。
