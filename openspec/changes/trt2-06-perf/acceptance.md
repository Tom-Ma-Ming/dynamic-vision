# 验收表

| 输入/动作 | 可观察结果 |
|---|---|
| `vision_detect` 同一图片前后对比 | 框类别、数量、分数、坐标完全一致 |
| `trt_infer` 同一 .f32 前后对比 | 输出张量逐元素一致（浮点容差 0） |
| blocking 1000 帧前后对比 | frames.jsonl 稳定字段逐字段一致 |
| `verify_replay.py` | 两轮哈希一致，overlay 可解码 |
| benchmark 同机前后各 3 轮 | 报告 infer/result_age P50/P95/P99；改善或至少无回退，并记录噪声 |
| 异常/销毁 | 无 CUDA 错误；pinned/device 缓冲释放 |
| windows-cpu / windows-sdk CTest | 当时 20/20 与 3/3 不退化；SIM-01 接入后 CPU/full 为 21/21 |
| pytest | 10/10 不退化 |

实测（2026-11-07）：vision_detect 2 框完全一致；trt_infer 50400 字节逐字节一致；before/after 各 3 轮 1000 帧稳定字段全部与 first 一致；verify_replay 哈希 25c13759… 不变；result_age P50 14.34→12.90ms、infer P50 3.98→3.53ms；windows-cpu 20/20（SIM 后 21/21）、windows-full 20/20（SIM 后 21/21）、windows-sdk 3/3、pytest 10/10。证据 artifacts/bench/ 与 docs/performance.md。

证据写入 artifacts/bench/ 与 docs/performance.md。
