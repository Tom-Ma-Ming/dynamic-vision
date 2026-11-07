# 连续录像接入与初检 · 2026-11-07

用户提供的 4 个 DXGI 来源 AVI 已完成全量解码、CV/IMM 离线回放及完整性验证。原始文件保持在 docs，未修改、未加入训练、未提交大文件。当前结论是工程链路通过，尚无人工身份真值，不能宣称真实跟踪精度或召回率达标。

## 素材检查

全部为 320×320、容器标称 30 FPS，共 21,169 帧 / 705.633 秒（约 11 分 46 秒）。

| 文件 | 帧数 | 时长 s | 相邻完全重复帧对 | 最长重复段帧数 |
|---|---:|---:|---:|---:|
| rec_20261107_202107.avi | 5400 | 180 | 140 | 52 |
| rec_20261107_202949.avi | 5400 | 180 | 125 | 17 |
| rec_20261107_203249.avi | 5400 | 180 | 171 | 18 |
| rec_20261107_203549.avi | 4969 | 165.633 | 100 | 16 |

实际解码帧数均等于容器声明，媒体时间严格递增，间隔约 33.333 ms。第一段仅 frame 0 是全黑；第 6～57 帧画面完全相同（52 帧），不是 52 帧黑画面。前期口头称“1.7 秒黑帧”不准确，以本记录和 first-video-duplicate-details.json 为准。

重复帧只能说明解码像素相同，不能断定采集掉帧。没有独立 DXGI 采集时间戳，因此容器 30 FPS 不证明实际采集时间均匀。SHA-256、尺寸、时间及抽样联系表保存在 artifacts/real-recordings-v1/intake。抽样可见人物进出、近远距离、遮挡、镜头移动；未据此确认存在足够的固定镜头急停事件。

后三段在命名时间与画面内容上呈现连续录制特征；在来源确认前按同一采集批次保留为验证资料，不按文件随机拆成训练/测试集。

## CV/IMM 回放结果

使用同一 best-fp32.engine、相同检测阈值、blocking 队列，分别传 configs/tracker.yml 和 configs/tracker-imm.yml。8 次均正常结束，各 processed=captured=原视频帧数，replaced/rejected/pending/stale/failed 均为 0。全部 42,338 个输出帧解码通过；各视频的 CV/IMM 检测、源帧号和媒体时间逐帧相同，IMM 概率有限非负且归一化。

| 视频末尾时间编号 | CV 自动 ID 总数 | IMM 自动 ID 总数 | CV 有 Confirmed 轨迹帧数 | IMM 有 Confirmed 轨迹帧数 |
|---|---:|---:|---:|---:|
| 202107 | 649 | 567 | 1250 | 1364 |
| 202949 | 451 | 334 | 1640 | 1744 |
| 203249 | 522 | 428 | 1844 | 1979 |
| 203549 | 470 | 363 | 1729 | 1873 |
| 合计 | 2092 | 1692 | 6463 | 6960 |

自动 ID 包含 Tentative 和误检轨迹，不等于真实人物数或 IDSW。两者出现身份歧义的帧数分别为 1805 / 2195；IMM 新建 ID 更少，但歧义帧更多，不能据此宣称身份更准确。8757 帧含 person 检测也不等于召回率：其余帧可能确实无人，也可能漏检。

人工抽样查看叠加图可见背景误检，例如第一段 frame 1860 的上方建筑区域、frame 3930 附近的箱体/墙面细节出现 person 框。快速镜头移动后也有 Lost 预测框漂移和新建轨迹。现有 IMM 无法解决错误观测本身；保留默认关闭，不根据无真值统计调参或训练。

## 已准备的复核材料

artifacts/real-recordings-v1/review 下有两个困难样例包，每包 90 帧（3 秒），所有 PNG 均逐像素等于源录像相应帧：

| 目录 | 来源 | 原始帧区间（右端不含） | 时间段 |
|---|---|---|---|
| clip01 | rec_20261107_202107.avi | [3930,4020) | 131～134 s |
| clip02 | rec_20261107_202949.avi | [540,630) | 18～21 s |

每包含 comparison.mp4（原图/CV/IMM 三栏）、连续原图 PNG、同名 X-AnyLabeling JSON、classes.txt、来源映射 manifest.json 和中文 README。候选框来自原检测输出，group_id 全为空；没有将自动轨迹 ID 伪装为真值。JSON 按项目既有人工标注结构生成，已校验配对与坐标；尚未在用户 X-AnyLabeling 界面验收。

用户可先观看 comparison.mp4，不需重新采集。复核时通过原图及同名 JSON 加载候选框，删除误框、补漏框、调整 head/person。为评估身份，同一人物跨帧必须保持同一 group_id，头和对应人物使用同一 ID；不确定的遮挡重现单独记录待确认，不猜身份。完全遮挡不凭空补框。保留 JSON 和原文件名，建议另存 reviewed 子目录；仅导出 YOLO HBB 会丢失身份信息。

这些是根据自动检测密度和目视问题挑选的诊断窗口，存在选择偏差，不能单独用于整体准确率结论。身份复核后才计算该片段 HOTA/IDF1/IDSW；代表性随机抽样、负样本完整标注及真实急停分层验收仍是后续工作。

## 复现

设置 docs/image-replay.md 中的 OpenCV/TRT/CUDA PATH。输出使用新目录，不覆盖现有证据。

```powershell
.\.venv\Scripts\python tools/inspect_recordings.py docs artifacts/real-recordings-repro/intake
# 对每个原视频分别执行，最后一个参数决定 CV/IMM。
out/windows-sdk/apps/trt_infer/Release/vision_replay.exe artifacts/engines/yolo11n-v002-fp32/best-fp32.engine docs/rec_20261107_202107.avi artifacts/real-recordings-repro/replay/rec_20261107_202107-cv configs/tracker.yml
out/windows-sdk/apps/trt_infer/Release/vision_replay.exe artifacts/engines/yolo11n-v002-fp32/best-fp32.engine docs/rec_20261107_202107.avi artifacts/real-recordings-repro/replay/rec_20261107_202107-imm configs/tracker-imm.yml
# 四段都回放完后验证。
.\.venv\Scripts\python tools/verify_recorded_replays.py artifacts/real-recordings-repro/intake/manifest.json artifacts/real-recordings-repro/replay artifacts/real-recordings-repro/verification.json
.\.venv\Scripts\python tools/prepare_recording_review.py docs/rec_20261107_202107.avi artifacts/real-recordings-v1/replay/rec_20261107_202107-cv artifacts/real-recordings-v1/replay/rec_20261107_202107-imm artifacts/real-recordings-repro/review/clip01 --start 3930 --frames 90
.\.venv\Scripts\python -m pytest tests/tools/test_inspect_recordings.py -q
```

原始视频、Engine 和 artifacts 是本机证据，未加入 Git；移机时需另行复制或重建。未修改 C++、模型、默认参数或用户已有的合成训练工作。

工具回归：本次检查工具 2 项，加上既有导入、报告、边界与 benchmark 工具测试，共 13/13 通过。
