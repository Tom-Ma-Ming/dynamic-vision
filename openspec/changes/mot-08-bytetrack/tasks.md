# 执行任务

- [x] B01 Config 增加 byte_track/high_score/low_score，默认关闭且兼容旧 YAML。
- [x] B02 Tracker 增加 high/low 两阶段关联，low 不 birth、不确认 Tentative。
- [x] B03 vision_replay 在 byte_track=1 时降低检测阈值。
- [x] B04 单元测试：low 不确认 Tentative、low 不 birth、Confirmed 可被 low 二次关联。
- [x] B05 SIM-01 低置信度场景基线/ByteTrack 对比，更新 docs 与交接。
