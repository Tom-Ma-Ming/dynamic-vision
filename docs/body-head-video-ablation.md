# 头身歧义分离：真实录像对照 · 2026-11-08

2026-11-08 身份语义补充：用户确认同一人物可能在遮挡/离开/场景变化后换group_id。以下分数衡量现有局部编号标签的一致性，不证明持续身份或长遮挡重识别能力。场景边界和同人关联尚待侧表确认；原数据与历史分数保留。详见 annotation-identity-semantics.json。

本轮仅切换 `separate_head_ambiguity`，比较 `tracker-imm.yml` 与 `tracker-body-primary.yml`。默认配置不变。两段完整录像各5400帧，检测结果、帧号和采集时间逐帧相同；输出视频各解码5400帧，无漏帧。跟踪从录像开始运行，使用冻结的两个90帧人工复核窗口评分，没有重训或改变阈值。

| 窗口 | 配置 | HOTA % | IDF1 % | IDSW |
|---|---|---:|---:|---:|
| clip01 | IMM | 42.61 | 58.49 | 4 |
| clip01 | IMM + 头身歧义分离 | 42.61 | 58.49 | 4 |
| clip02 | IMM | 30.94 | 34.43 | 26 |
| clip02 | IMM + 头身歧义分离 | 30.94 | 34.43 | 26 |

整段录像按每帧每条 Confirmed 轨迹计数，第一段身份歧义106→63，第二段107→89；新配置分别记录43和18个头部冲突状态。窗口内为3→1、7→7。减少身份歧义没有改变这些窗口的身份和框匹配分数，不能称为精度提升。未标注区间只能证明日志行为，不能证明身份判断正确。现有框范围/身份真值仍属于 `provisional_diagnostic_not_acceptance`。

回放JSON新增 `head_ambiguous` 和 `body_track`：前者表示最后匹配观测存在头部候选冲突，后者表示轨迹已使用人物框锚点；两者不是当前头部测量可用性，也不是轨迹出生类别。Lost帧可能保留最近观测状态。`head_ambiguous=false` 不能解释为当前检测到有效头部；完整头部测量有效性/时间戳仍待实现。旧日志没有该字段，不能把缺失当作旧算法没有头部冲突。

视频叠加 `head?` 表示头部候选冲突，原来的 `?` 仍表示人物身份歧义。已查看第一段3993帧导出图，右侧人物轨迹的头部冲突提示可见；这只是显示核验，不是人工确认检测框正确。

证据目录：`artifacts/real-recordings-v1/body-primary-evaluation-v1`，包括 `report.json`、`full-video-audit.json`、冻结标签、逐帧评分输入和 `head-conflict.png`。预测来自 `artifacts/real-recordings-v1/replay/*-body-primary`；输入配置和Engine摘要见审计JSON。

复现时先用 `vision_replay engine video new-output configs/tracker-body-primary.yml` 回放两个完整视频（输出目录后缀 `-body-primary`），再执行：

```powershell
.\.venv\Scripts\python tools/evaluate_reviewed_clips.py artifacts/real-recordings-v1/evaluation-20261108-v1/scores-final/labels artifacts/real-recordings-v1/replay 新评分目录 --quality-notes artifacts/real-recordings-v1/evaluation-20261108-v1/quality-notes.json --methods imm body-primary
```

本轮重新构建 `vision_replay`，SDK CTest6/6通过。评分工具提示可选BURST依赖缺失，实际使用的HOTA、CLEAR、Identity评分正常完成。该实验不覆盖长遮挡ReID、完整头部可用性或代表性整体精度验收。

## 2026-11-08 头部测量时间契约补齐

新增 head_measured / last_head_observed_ts_ns，可靠头测量才更新时间；person单独更新、头候选或身份关联冲突、Lost及预测均不伪造新测量。覆盖CV/IMM、普通关联/ByteTrack两阶段、时间0、单独头部、共享头归属冲突与恢复。head+为本帧测量，head?为头候选冲突，head-为无本帧可靠测量；Tentative仍需下游状态门控。

完整5400帧回放与上轮body-primary基线的检测、轨迹既有字段和选择逐帧完全相同。新增测量记录1220，历史4571，从未测量2008（逐帧全部轨迹计数，含Lost，非准确率）。视频完整解码并查看3993帧叠加。证据 artifacts/real-recordings-v1/head-measurement-v1/verification.json。CPU27/27、SDK6/6、Python33/33通过；增加ByteTrack低分阶段断言后，两套构建的CV/IMM测试再次通过。此前文档“测量有效性未完成”描述由本条更新；仍不证明头部检测正确、ReID或最终控制精度达标。
