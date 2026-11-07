# 设计

## 输入与发现

工具扫描 artifacts 根目录，优先使用以下固定位置，缺失时回退到 `runs/*`、`engines/*`、`replay/*` 的排序结果：

- `runs/<run>/baseline.json`、`person-diagnosis.json`、`weights/onnx-contract.json`
- `engines/<engine>/manifest.json`、`verification.json`、`image-check.json`
- `replay/<fixture>/synthetic-metrics.json`
- `replay/**/summary.json`，以及 `track02/*-summary.json`
- `datasets/yolo_v002/summary.json`、`validation-*/labeled-review/quality.json`
- `track02/cpu-ctest.log`、`windows-sdk-ctest.log`、`windows-full-ctest.log`、`pytest.log`、`verify-replay.txt`、`blocking-compare.txt`、`realtime-*-invariants.txt`
- 仓库根目录的 `docs/environment-lock.md` 表格

每个输入文件记录相对路径和 SHA-256；报告写入 `inputs`。扫描结果排序，保证同一输入产生相同顺序。

## 报告结构

`report.json`：

- `schema_version`、`generated_at`、`tool`、`artifacts_root`
- `environment`：从 environment-lock.md 表格解析的键值
- `data`：train/val 图像与类别框数、负样本/空标签计数、场景元数据状态
- `detection`：overall 与逐类指标、conf 0.25/IoU 0.5 操作点、尺寸桶、looser IoU
- `engine`：manifest 摘要、SHA、GPU/SDK、verification 样本的最大绝对误差、image-check 最大像素误差
- `mot`：合成序列 HOTA/IDF1/IDSW、数据来源标签（synthetic 或 real）
- `replay`：每个 summary 的路径、模式、容量、帧计数、P50/P95/P99
- `evidence`：CTest/pytest/verify-replay/invariants 的解析结果
- `gates`：T01–T12 的状态、证据、缺口说明
- `gaps`：召回目标、负样本、实景身份真值、多场景、远程 CI、长时资源、IMM/镜头补偿等
- `inputs`：文件相对路径、SHA-256、字节数
- `summary`：总体状态、通过/部分/缺失计数、关键数字

## 分层与真实性

- 检测按类别和尺寸桶；尺寸桶只使用 person-diagnosis.json 已有数据。
- MOT 只把 synthetic-metrics.json 标为“合成序列”；没有实景身份真值时，实景 MOT 明确为缺失。
- 延迟按 summary 的 mode/capacity/paced 分组；legacy summary 缺字段时标为 legacy，不推断。
- 数据场景数、负样本、远程 CI 等无法从现有文件证明的项，状态为 unknown 或 missing。
- 不设置精度通过阈值；只对已明确的工程门槛（如 T10 原始张量容差、T11 重复回放）标记通过/缺失。

## 输出

`report.json` 用 UTF-8、缩进 2、固定键顺序；`report.md` 用中文表格；`report.html` 为单文件、无外部资源、HTML 转义。`--strict` 要求检测、Engine、MOT、至少一个 TRACK-02 summary、verify-replay、CPU CTest、pytest 全部存在，否则返回 1；默认模式只报告缺失，返回 0。

## 回退

报告是只读聚合，不修改 artifacts；删除输出目录即可回退。若某个 JSON 结构变化，解析函数返回 missing 而不是猜测。
