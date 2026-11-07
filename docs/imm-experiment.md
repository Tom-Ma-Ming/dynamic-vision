# IMM 实验验收 · 2026-11-07

已实现默认关闭的三模式 IMM。急停、变向的合成结果改善，小目标平稳误差仍高于原始 CV，因此没有替换默认 tracker.yml。真实录像身份准确率、person 检测召回率不由本实验证明。

## 实现与使用

每条轨迹维护 stationary、CV、maneuver 三个四维高斯状态，单位为像素、像素/秒；预测使用采集时间戳差值，秒。stationary 按时间常数衰减速度；CV 使用原 acceleration_sigma；maneuver 使用较高过程噪声。时间相关的对称马尔可夫转移先混合均值及协方差，再预测；观测更新使用 Joseph 形式和对数似然，融合保留模型间均值差异项。

模式先验概率达到门槛的任一模型可通过马氏距离门控；代价使用这些模型的最小距离，继续应用全局一对一分配、歧义拒绝和原生命周期。这可能扩大关联范围，真实多人录像仍需验证。ByteTrack 两阶段复用相同更新。头部轨迹转人物时，三个模型共同平移锚点。

configs/tracker-imm.yml 显式启用：stationary acceleration=30、CV=180、maneuver=900 px/s²，stationary tau=0.08 s，模型间跳转率=1/s，门控最低模式概率=0.05。开启时参数必填且有限正值，概率门槛不得超过 1/3。关闭时保留原 CV 分支及 JSON 字段。

启用后的 JSONL 轨迹增加 mode_probabilities，顺序固定为 [stationary, CV, maneuver]。predictAt 返回独立快照，仿真预测评分和延迟补偿均使用 IMM 外推。默认配置不增加此字段。实现只涉及离线回放与自建仿真。

## 三种子消融

所有方法用 configs/sim-control.yml，seed 偏移 0、100、200，12 场景完整窗口。观测、真值、窗口长度相同；tools/verify_sim_metrics.py 独立复算控制误差、预测误差及覆盖率。下表是三个种子的完整窗口控制 MAE 均值，包含未获得轨迹期间的控制误差。

| 场景 | CV px | 机动 CV px | IMM px | CV 覆盖率 | IMM 覆盖率 |
|---|---:|---:|---:|---:|---:|
| 匀速 | 14.525 | 11.420 | 10.570 | 98.3% | 99.2% |
| 急停 | 12.566 | 11.150 | 9.358 | 81.4% | 99.2% |
| 变向 | 95.652 | 8.447 | 7.612 | 58.8% | 99.3% |
| 小目标 | 9.173 | 9.883 | 9.597 | 99.2% | 99.2% |
| 低置信度 | 9.343 | 10.674 | 9.719 | 99.0% | 99.0% |

为排除启动误差掩盖稳态退化，另按固定帧窗口比较控制误差（零基帧号、右端不含）：

| 窗口 | CV px / 覆盖率 | 机动 CV px / 覆盖率 | IMM px / 覆盖率 |
|---|---:|---:|---:|
| 急停后 [40,65) | 7.373 / 88.0% | 4.232 / 100% | 2.087 / 100% |
| 变向后 [70,95) | 71.807 / 53.3% | 5.225 / 100% | 5.114 / 100% |
| 小目标稳定段 [20,120) | 0.839 / 100% | 1.634 / 100% | 1.250 / 100% |

IMM 对小目标平稳段仍有约 0.411 px 退化，不能认为所有场景改善。模式概率确实发生切换：seed 偏移 0，急停 frame 39 的 stationary 概率约 0.107，frame 42 为 0.651；变向 frame 69 的 maneuver 概率约 0.099，frame 72 为 0.664。

0.1 秒预测 MAE（只统计轨迹可用且未来真值存在的帧）：急停机动 CV 4.023→IMM 1.964 px，变向 4.493→3.578 px。该指标有条件采样，须结合覆盖率看，不能与控制全窗口误差混用。

成功阈值没有放宽。IMM 每个种子的 12 场景均为 SUCCESS 1 / FAILED 10 / UNKNOWN 1。误差改善不等于达到严格闭环成功条件，也不等于人物召回达到 95%。

## 工程回归

- CPU CTest 25/25、SDK CTest 5/5、Python pytest 11/11。
- 数值测试覆盖融合/Joseph 更新解析解、dt=0、不等间隔、丢观测、开/关 ByteTrack、模式概率归一化、各模式及融合协方差对称半正定、预测快照无副作用。
- 既有 MOT 测试在 IMM 下重跑：1/2/5/10 人交叉、头身转换、遮挡恢复、失效、选择、过期结果以及两阶段身份歧义。
- 关闭 IMM：两轮各 1000 帧稳定字段一致，且逐字段等于修改前 review-fixed/first/frames.jsonl。
- 开启 IMM：两轮各 1000 帧稳定字段一致，模式概率有限、非负、和为 1（JSON 精度容差 1e-5），叠加视频逐帧可解码。重复截图夹具只用于工程回归，不是连续运动精度证据。

## 复现与证据

在项目根目录按 docs/image-replay.md 设置 OpenCV/TRT/CUDA PATH，输出目录使用新目录：

```powershell
cmake --build --preset windows-cpu
ctest --preset windows-cpu --output-on-failure
cmake --build --preset windows-sdk
ctest --preset windows-sdk --output-on-failure
.\.venv\Scripts\python -m pytest tests/tools -q
.\.venv\Scripts\python tools/ablate_sim_control.py out/windows-cpu/apps/sim_runner/Release/sim_runner.exe configs/sim-control.yml configs/tracker.yml configs/tracker-imm.yml artifacts/sim/imm-repro-cv
.\.venv\Scripts\python tools/ablate_sim_control.py out/windows-cpu/apps/sim_runner/Release/sim_runner.exe configs/sim-control.yml configs/tracker-cv-maneuver.yml configs/tracker-imm.yml artifacts/sim/imm-repro-maneuver
.\.venv\Scripts\python tools/verify_replay.py out/windows-sdk/apps/trt_infer/Release/vision_replay.exe artifacts/engines/yolo11n-v002-fp32/best-fp32.engine E:/mac-share/tensorRT_Pro-main/tensorRT_Pro-main/windows/workspace/data_collect artifacts/replay/imm-repro-off --reference-frames artifacts/replay/review-fixed/first/frames.jsonl
.\.venv\Scripts\python tools/verify_replay.py out/windows-sdk/apps/trt_infer/Release/vision_replay.exe artifacts/engines/yolo11n-v002-fp32/best-fp32.engine E:/mac-share/tensorRT_Pro-main/tensorRT_Pro-main/windows/workspace/data_collect artifacts/replay/imm-repro-on --tracker configs/tracker-imm.yml
```

本机证据：artifacts/sim/imm-v1-vs-cv、imm-v1-vs-maneuver 的 comparison.json 与各 seed 的 runs.jsonl/summary.json；窗口汇总 artifacts/sim/imm-event-windows.json；artifacts/replay/imm-disabled-v1、imm-enabled-v1；artifacts/code-review/imm-* 日志。artifacts 按仓库约定忽略，异机需单独转移或按命令重建；代码、配置、测试和本报告入库。

后续优先用独立连续录像和身份真值确认收益，再决定是否标定参数或启用。镜头补偿、ReID、负样本、person 召回 95% 和远程 CI 实跑仍未完成。
