# 执行任务

- [x] P01 Detector 增加 pinned host 输入/输出与 `input()`/`infer()` 新 API。
- [x] P02 修改 image_detection、vision_detect、trt_infer 调用点，去掉每帧大 vector 分配。
- [x] P03 增加 tools/benchmark_replay.py，保存 before/after 各 3 轮结果。
- [x] P04 验证 vision_detect 框、trt_infer 原始输出、1000 帧稳定字段不变。
- [x] P05 跑 CPU/windows-sdk CTest、pytest、eval_report，更新 docs 和 EVAL-01 报告。result_age P50 14.34→12.90ms，infer P50 3.98→3.53ms。
