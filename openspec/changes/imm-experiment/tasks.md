# 任务

- [x] IMM-01：src/tracking/imm.* 混合/预测/Joseph 更新/融合；tests/unit/test_imm.cpp 数值测试，ctest --preset windows-cpu，证据 artifacts/code-review/imm-final-cpu-ctest.log。
- [x] IMM-02：tracker.* 可选模式、两阶段关联、锚点及快照；CTest mot_imm 复用交叉/遮挡/歧义测试，证据同上与 imm-final-sdk-ctest.log。
- [x] IMM-03：JSON 模式概率及模拟外推；tools/ablate_sim_control.py 与两种 CV 对照各三个 seed 偏移；独立复算通过，证据 artifacts/sim/imm-v1-vs-{cv,maneuver}。
- [x] IMM-04：tools/verify_replay.py 增加 tracker、参考字段对照、概率检查；关闭/开启各双跑 1000 帧，关闭等于旧基线，证据 artifacts/replay/imm-{disabled,enabled}-v1。
- [x] IMM-05：docs/imm-experiment.md 记录参数、事件/平稳段结果及退化；更新进度与交接。
- [ ] 后续独立验收：真实连续录像身份指标；不得将工程回归视作真实精度达标。
