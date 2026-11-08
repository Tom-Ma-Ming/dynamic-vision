# 任务

- [x] CMC-01：src/tracking/camera_motion.*、configs/tracker-camera.yml 实现背景平移、参数与质量门控，默认关闭。
- [x] CMC-02：tracker.cpp 与 replay.cpp 接入 CV/IMM/ByteTrack、独立 camera_ms 和质量遥测。
- [x] CMC-03：tests/unit/test_camera_motion.cpp 覆盖已知平移、独立前景、无/局部纹理、突变、时间、头身与状态一致性；CPU 26/26、SDK 6/6。证据 artifacts/camera-motion-v1/*tests-final.log。
- [x] CMC-04：关闭/开启各两轮 1000 帧重复一致，关闭等于旧基线；两段原录像 10800 帧完整回放及同检测对照，冻结 180 帧评分完成。证据 disabled-replay、enabled-repeat、verification.json、scores。
- [x] CMC-05：docs/camera-motion-experiment.md 与 docs/task-roadmap.md 记录收益、退化、覆盖率及耗时，保持默认关闭。
