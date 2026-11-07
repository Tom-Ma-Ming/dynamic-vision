# 任务

- [x] RV-01：tools/inspect_recordings.py 全量解码、摘要与抽样预览；21,169 帧通过，2 项工具测试通过；证据 artifacts/real-recordings-v1/intake。
- [x] RV-02：4 个录像的 CV/IMM blocking 回放与逐帧完整性对照；42,338 输出帧通过，检测逐帧一致；证据 artifacts/real-recordings-v1/replay 和 verification.json。
- [x] RV-03：两个 90 帧复核包，原图像素/JSON 配对/对照视频通过检查；结果与局限见 docs/real-recording-intake.md。
- [ ] 后续：人工确认连续人物身份与漏检，才可计算真实跟踪/召回指标。
- [x] RV-04（2026-11-08）：接收 180 帧人工标注并创建源快照；11 处确定问题在独立副本修正，审计工具 3 项测试通过。见 docs/reviewed-label-audit.md。
- [ ] RV-05：统一 person 可见人体框范围，核对跨遮挡身份后验收 GT；目前不发布真实 MOT 分数。
