# 接手复核与整改记录 · 2026-11-07

复核对象为 docs/handoff-grok.md。方式是读文档后实跑 CTest、Engine 推理、1000 帧回放、pytest，并检查 Git 与构建入口；不是只做文字对照。

## 已整改

| 交接缺口 | 处理 |
|---|---|
| 66 个实现文件未入库，HEAD 只有 3 个文档 | 已提交检查点 `900c440`；本记录与后续构建/文档修复另提交 |
| 远程 CI 实际不可用 | 工作流增加 Windows probe 与 Ubuntu CPU 两个 job；CPU job 跑 pytest、CMake 构建和 CTest |
| docs/design.md、docs/plans/phase0-1.md 与新实现冲突却自称基线 | 两页顶部加归档横幅；总设计复制到 docs/FPS_Vision_Tracking_TensorRT_Official_v2.0.md |
| windows-sdk 只建探针和 TRT 应用，vision_core/GTest 从未编译 | 新增 windows-full（完整构建）与 windows-cpu（无 TRT 的 CPU 全量测试）；mot_test 移到根 CMakeLists |
| PicoSHA2 目标重复、MSVC 代码页 936 编译含中文注释的源文件失败 | 删除重复包装，改用上游 INTERFACE target；MSVC 统一加 `/utf-8` |
| OpenSpec 只有单行摘要 | 新增 openspec/README.md 格式要求；TRACK-02 已有 proposal/design/tasks/acceptance |
| pytest 无法按文档复现 | 新增 requirements-dev.txt；本机 .venv 已装 pytest 8.3.5，tests/tools 5/5 通过 |
| 文档小错 | image-replay.md 修正计时范围；mot-replay.md 修正 mot_test 路径；verify_replay.py 改为稳定字段哈希 |

## 实测证据

- windows-cpu 等价配置（out/windows-cpu-review，OpenCV 4.13，无 TRT）：CTest 4/4，0.53s；包含 Smoke.ThirdPartyLinks、mot_synthetic、probe_version、probe_environment。
- windows-sdk（TRT 8.6.1.6 + OpenCV）：CTest 3/3，1.58s。
- 1000 帧回放：重建后的 vision_replay 重放 artifacts/replay/mot-v1/fixture.avi，1000 帧；去掉 detect_ms 后 frames.jsonl 与 first/、second/ 既有证据逐字段一致。
- Engine 推理：vision_detect 载入 best-fp32.engine，val 首图输出 2 个 person 框。
- Python：`python -m pytest tests/tools -q` 5 passed。
- 数据：train 402 图/402 标签（head 418、person 415），val 42 图（各 59）。

## 本机注意

- 在受限文件沙箱下，MSBuild 的 file tracker 会报 TRK0002；本地构建可加 `/p:TrackFileAccess=false`，或使用完整权限。该限制不影响仓库代码和 CI。
- windows-cpu / windows-full 预设不内置 OpenCV 路径，需追加 `-DOpenCV_DIR=F:/opencv/opencv/build`。

## 仍待

- 远程 GitHub Actions 尚未实际运行。
- TRACK-02 实现未开始；OpenSpec 已写，按 proposal/design/tasks/acceptance 推进。
- 真实录像、长时资源趋势、真实身份标注和 person 召回 95% 仍按原交接后置。
