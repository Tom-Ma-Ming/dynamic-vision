# 人工标注片段诊断评估 · 2026-11-08

已读取用户明确指定的 artifacts/real-recordings-v1/review/clip01、clip02，而非旧 corrected-candidate。clip01 相较上次检查更新 34 帧（3986～4019），主要人物的可见腿脚已纳入框。共 180 帧结构检查通过，标注为 232 个 person、227 个 head，35 张空标注图。

本轮输出是 **provisional_diagnostic_not_acceptance**。抽样早段 clip01/3946、3950、3967 仍有可见肢体未被完整覆盖；clip02/549、565 等人物框范围仍略不一致。只完成结构检查、重点视觉复核及用户声明接收，未对所有人物逐像素独立验收。分数可用于排查，不能作为最终真值验收或整体精度结论。

## 数据与计分口径

- 两段各 3 秒，原视频区间为 clip01 [3930,4020)、clip02 [540,630)，零基帧号、右端不含；按片段隔离人物 ID。它们是人为挑选的困难窗口，不是随机代表性测试集。
- 冻结本轮实际读取的 PNG、JSON、映射至 evaluation-20261108-v1/scores-final/labels；保留每份图片和标注的 SHA-256、源视频和预测 JSONL 哈希、评分代码摘要、运行库版本。
- 原图逐帧与源录像解码像素一致。CV/IMM 使用已有完整录像回放状态，截取相同窗口评分；这是完整视频状态进入片段的评估，不是从片段第一帧重新启动跟踪器。
- 检测按类别计算，conf ≥0.25、IoU ≥0.5，最大合法匹配数量优先，再最大化 IoU，一对一匹配；没有训练或重新筛选阈值。
- 跟踪使用所有 Confirmed 框，包含身份歧义帧，排除 Tentative/Lost；不按人工 GT 挑选轨迹。现有 JSON 没有区分 head-origin/person-origin，统一轨迹框全部参与 person GT 对比，头部起始轨迹也可能被计为 FP，此限制保留在报告。
- HOTA 使用 TrackEval 默认 0.05～0.95 IoU 阈值；IDF1 和 CLEAR IDSW 使用 0.5。逐片段报告，不直接平均 HOTA，也不把两段 ID 拼成同一身份空间。

## 跟踪诊断结果

| 片段 | 模式 | HOTA % | IDF1 % | IDSW 次 |
|---|---|---:|---:|---:|
| clip01 | CV | 44.85 | 61.39 | 5 |
| clip01 | IMM | 42.61 | 58.49 | 4 |
| clip02 | CV | 23.77 | 25.00 | 24 |
| clip02 | IMM | 30.94 | 34.43 | 26 |

IMM 在 clip01 的 HOTA/IDF1 略降，clip02 两项上升但 IDSW 增加。不存在一致优势，继续默认 CV、IMM 保持实验开关。即使 IDSW 更少，也可能只是漏匹配更多，不能单独作为优胜依据；已知标注范围误差也可能影响排序。

## 检测诊断结果

两种跟踪配置的输入检测逐帧一致，所以检测分数只计算一份。

| 类别 | TP | FP | FN | Precision % | Recall % |
|---|---:|---:|---:|---:|---:|
| person | 206 | 71 | 26 | 74.37 | 88.79 |
| head | 122 | 36 | 105 | 77.22 | 53.74 |

分片段 person：clip01 为 75 TP / 62 FP / 16 FN，clip02 为 131 / 9 / 10。head：clip01 为 70 / 27 / 19，clip02 为 52 / 9 / 86。片段二的小目标头部检测仍是诊断重点；漏匹配也可能来自框定位偏差，而非完全没有候选框，需要结合具体帧看。

这不是模型从旧 person R≈0.712 提升到 0.888：模型没有改变，数据集、阈值和评分口径不同。不能由这 180 帧推断整体 person 召回达到 95%，也不能据此决定默认模型替换。

## 复现与验证

```powershell
.\.venv\Scripts\python -m pip install -r requirements-dev.txt
.\.venv\Scripts\python tools/evaluate_reviewed_clips.py artifacts/real-recordings-v1/review artifacts/real-recordings-v1/replay artifacts/eval-repro-new --quality-notes artifacts/real-recordings-v1/evaluation-20261108-v1/quality-notes.json
.\.venv\Scripts\python -m pytest tests/tools/test_evaluate_reviewed_clips.py tests/tools/test_audit_reviewed_clips.py -q
```

需精确复算本轮时，将命令中的 labels 根目录换成 artifacts/real-recordings-v1/evaluation-20261108-v1/scores-final/labels，避免后来用户修改源标注影响结果。输出目录必须不存在。证据为 scores-final/report.json、各片段/模式的 aligned.jsonl、冻结 labels，以及上级 clip01-labels.jpg、clip02-labels.jpg、quality-notes.json。

本轮相关工具测试 20/20（不含用户另行开发、未入库的合成训练测试）。新增测试覆盖重复检测一对一匹配、空帧、双匹配保留、时间错位拒绝，以及完美框下的身份断裂确实降低 HOTA/IDF1 并增加 IDSW。Python 评估依赖固定于 requirements-eval.txt 并纳入 dev requirements；pip dry-run 解析通过，但未宣称干净环境安装或远程 CI 已运行。

本机 TrackEval 导入时提示 BURST 的可选 pycocotools 缺失，本轮 HOTA/CLEAR/Identity 正常执行并通过解析预期测试；完整 requirements 安装会补齐该依赖。未修改 C++、默认参数、模型或用户原始标注，亦未将这批诊断数据加入训练。

下一步优先定位 head 漏匹配、背景 FP、镜头运动时的轨迹碎片；保留剩余标注范围/跨遮挡身份问题，最终验收前再统一。这轮无需重新录制或重做整批标注。
