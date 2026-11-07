# 验收

| 输入/动作 | 预期与实测 | 命令/证据 |
|---|---|---|
| 均值 -2/0/2、单位协方差融合 | 方差 1+8/3，误差 <1e-10；通过 | CTest imm_numeric |
| 相同模型观测更新、dt=0 | 均值 40/13、方差 36/13；dt=0 不变；通过 | CTest imm_numeric |
| 1000 步变向/急停/丢观测、不等 dt，两种关联模式 | 概率有限非负，和误差 <1e-10；各模式及融合协方差对称误差 <1e-7、最小特征值 >=-1e-8；预测无副作用；通过 | tests/unit/test_imm.cpp |
| IMM 重跑旧 MOT 测试 | 多人交叉、头身、遮挡、失效、两阶段歧义断言通过 | CTest mot_imm |
| 关闭 IMM 双跑 1000 帧 | 稳定字段重复一致且等于 review-fixed；通过 | tools/verify_replay.py，artifacts/replay/imm-disabled-v1 |
| 开启 IMM 双跑 1000 帧 | 稳定字段重复一致，概率和容差 1e-5，两轮视频各解码 1000 帧；通过 | 同工具 --tracker configs/tracker-imm.yml，imm-enabled-v1 |
| 三 seed × 12 场景、两种 CV 对照 | 同观测、完整窗口及误差复算通过；急停/变向改善、小目标退化已记录，保持默认关闭 | tools/ablate_sim_control.py，docs/imm-experiment.md |

本机 CPU 25/25、SDK 5/5、pytest 11/11。真实身份真值与 95% 检测召回未验收；合成指标不替代实景。
