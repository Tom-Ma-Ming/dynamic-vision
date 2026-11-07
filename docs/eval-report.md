# 统一分层评估报告（EVAL-01）

`tools/eval_report.py` 只读聚合已有 artifacts，不重新训练、不重建 Engine、不重跑 CTest。输出 JSON、Markdown 和单文件 HTML，缺失的分层会明确标为 missing/unknown，不用默认值填充。

## 用法

~~~powershell
# 默认：生成报告，缺失项只记录
.\.venv\Scripts\python.exe tools\eval_report.py artifacts --out artifacts/eval-report

# 严格：检测/Engine/MOT/TRACK-02 summary/verify-replay/CPU CTest/pytest 缺一即返回 1
.\.venv\Scripts\python.exe tools\eval_report.py artifacts --out artifacts/eval-report --strict
~~~

输出目录必须是新目录或空目录。报告写入 `report.json`、`report.md`、`report.html`。

## 报告内容

- 环境：从 docs/environment-lock.md 的表格解析。
- 数据：train/val 图像数、逐类框数、空标签/负样本、场景元数据状态。
- 检测：baseline 的 overall/逐类指标、conf 0.25/IoU 0.5 操作点、尺寸桶、looser IoU。
- Engine：manifest、GPU/SDK、ONNX/Engine SHA、原始张量 max_abs、image-check max_px。
- MOT：合成序列的 HOTA/IDF1/IDSW，并明确标注 synthetic；实景身份真值缺失单列。
- 回放：每个 summary 的 mode/capacity/processed/replaced/peak 与 result_age P50/P95/P99；legacy summary 缺字段时写 missing。
- 证据：CPU/windows-sdk/windows-full CTest、pytest、verify-replay、1000 帧 stable 对比、实时不变量。
- 验收矩阵：T01–T12 的 pass/partial/missing 与证据说明。
- 缺口：person 召回目标、负样本、场景分层、实景 MOT 真值、长时资源、远程 CI、IMM/镜头补偿。
- 输入清单：每个输入文件的相对路径、SHA-256、字节数。

## 当前基线（2026-11-07）

- 状态：`complete-with-gaps`；最新 review-fixed 报告 gates 6 pass / 4 partial / 2 missing。旧 pre-fix artifacts 的 7/3/2 不再作为当前精度结论。
- person Box-R 0.7119（目标 0.95）；合成 MOT HOTA 0.9370、IDF1 0.9714、IDSW 0。
- 仿真：review-fixed-v2-base 12 场景 SUCCESS 0 / FAILED 8 / UNKNOWN 4；严格目标尺寸与完整窗口下控制器尚未达标。IMM 每个 seed 为 1/10/1。
- T03、T06、T07、T08、T10、T11 通过；T01/T02/T05/T12 部分；T04 负样本、T09 真实急停/蹲下仍缺。
- 证据目录：`artifacts/eval-report-review-fixed/`（旧 `artifacts/eval-report/` 仅为历史）；仿真证据见 docs/sim-report.md、docs/review-fixes.md、docs/imm-experiment.md。

性能前后对比见 docs/performance.md；EVAL-01 当前不把 benchmark 混入检测/MOT 指标，避免把链路延迟当作模型精度。
