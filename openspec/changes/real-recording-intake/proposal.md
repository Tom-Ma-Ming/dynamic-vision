# 真实录制验证资料接入

用户提供 docs/rec_*.avi，用于验证连续画面的跟踪行为。只读检查全部视频，运行默认 CV/IMM 离线回放，准备身份复核候选片段；不并入训练、不把自动轨迹当身份真值。

入口：tools/inspect_recordings.py、现有 vision_replay、artifacts/real-recordings-v1。后续真实 HOTA/IDF1/IDSW 与召回评估需要人工复核的真值。
