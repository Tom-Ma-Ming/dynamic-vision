# 人工复核标注接收检查 · 2026-11-08

已发现并接收用户原位保存的 clip01/clip02 标注：各 90 张 PNG 和同名 JSON，共 180 帧。全部 PNG 与原视频对应帧逐像素相同。JSON 版本为 4.2.0，原有 description 中 UNREVIEWED 字样及 checked=false 是工具遗留字段，不单凭它们否定用户已复核的声明。

本轮状态：结构检查与确定的笔误修正已完成；可见人体框覆盖范围和跨遮挡身份语义尚未完成验收，不训练、不发布真实 MOT 指标。

## 原始标注与修正副本

根目录 artifacts/real-recordings-v1/label-audit-20261108：

- source-snapshot：原始人工标注和 PNG 的完整副本，字节与用户源文件一致。
- source-audit.json：原始配对、元数据、坐标、类别及 ID 检查，每个文件附 SHA-256。
- corrected-candidate/clip01、clip02：修正 11 处确定问题的独立副本，未改写用户源文件。
- corrections.json：逐项保存修改前后 shape、原文件名和原因。
- candidate-audit.json：副本 180 帧结构错误为 0，含 232 个 person、228 个 head，35 张空标注图；clip01 使用 ID 1～5，clip02 使用 ID 1～2，ID 作用域按片段隔离。
- body-extent-example.jpg：person 未覆盖可见腿脚的示例。

修正依据为实际图像、同帧头身对应关系；没有使用自动跟踪 ID 生成身份真值。坐标除一处轻微越界裁切外均未重新绘制。

| 片段 / 源帧号 | shape 序号（从 1 开始） | 修正 |
|---|---:|---|
| clip01 / 3946 | 2 | head 右边界 320.489 裁到 320 |
| clip01 / 3967 | 1 | 头部小框 person → head，ID 2 不变 |
| clip01 / 3986 | 2 | 头部小框 person → head，ID 5 不变 |
| clip01 / 3987 | 2 | 头部小框 person → head，ID 4 不变 |
| clip01 / 4001 | 1 | 人物大框 head → person，ID 5 不变 |
| clip02 / 549 | 4 | head ID 2 → 1，与其人物对应 |
| clip02 / 551 | 4 | head ID 1 → 2，与其人物对应 |
| clip02 / 565 | 2 | person 空 ID → 2 |
| clip02 / 574 | 1 | person ID 2 → 1 |
| clip02 / 593 | 2 | person ID 2 → 1 |
| clip02 / 594 | 1 | person 空 ID → 1 |

## 尚未通过的视觉质量项

总设计 §4.1 规定 person 框覆盖可见人体区域，而非仅上半身。抽样中的 clip01 / 3946、3986、3987、4001 等帧，可见的腿脚在 person 框外；例如 3986 中右侧 ID 5 和左侧 ID 4 都有下肢未被覆盖。此类问题不会被“框坐标合法”或“头在人物框内”检查发现，却会影响 IoU、检测召回及 MOT 评价。

应在 corrected-candidate 副本中继续复核 person 的上下左右边界：覆盖实际可见人体，包括头部和可见腿脚；不补全遮挡后不可见身体。不是把框机械扩到画面底部，也不是按固定比例推算。检查两组所有 person，不能只修示例帧后宣称全部通过。

group_id 的形式已有效，但跨遮挡重现是否为同一人物不能仅靠数字检查证明。clip01 的进出画面和重现仍需人工按原录像确认；不要自动按距离或角色外观合并 ID。现阶段不输出 HOTA/IDF1/IDSW，以免把形式正确但语义未验收的标签当成最终真值。

这 180 帧仍是困难诊断样例，不并入训练，也不能用于宣称整体 95% 召回。

## 复现及测试

```powershell
# 输出文件必须不存在；结构错误返回非零退出码。
.\.venv\Scripts\python tools/audit_reviewed_clips.py artifacts/real-recordings-v1/review artifacts/source-audit-new.json
.\.venv\Scripts\python tools/audit_reviewed_clips.py artifacts/real-recordings-v1/label-audit-20261108/corrected-candidate artifacts/candidate-audit-new.json
.\.venv\Scripts\python -m pytest tests/tools/test_audit_reviewed_clips.py -q
```

新增测试 3/3：头/身共享 ID 不误判重复；同类重复 ID 与空 ID 检出；越界及缺文件检出；只读检查不改变源 JSON。结构检查通过不等于框质量或身份连续性验收通过。
