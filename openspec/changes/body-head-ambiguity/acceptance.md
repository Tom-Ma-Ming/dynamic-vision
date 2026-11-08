# 验收
CPU CTest27/27，CV/IMM 与普通/ByteTrack 覆盖单身双头时保持人物状态、拒绝冲突头锚点匹配。TensorRT 图像夹具 ready17→99/100；UNKNOWN→FAILED，MAE21.9574→21.9088。只解决门控语义，不证明头部定位或任务成功。跨场景录像回归和完整头部可用性输出仍待后续。

真实录像对照已完成：两段10800帧检测逐帧相同；180帧诊断窗口跟踪分数未变，身份歧义计数减少。输出冲突状态及视频提示已核验，完整头部测量有效性与时间戳仍未完成。见 docs/body-head-video-ablation.md。

## 2026-11-08 头部测量时间契约补齐

新增 head_measured / last_head_observed_ts_ns，可靠头测量才更新时间；person单独更新、头候选或身份关联冲突、Lost及预测均不伪造新测量。覆盖CV/IMM、普通关联/ByteTrack两阶段、时间0、单独头部、共享头归属冲突与恢复。head+为本帧测量，head?为头候选冲突，head-为无本帧可靠测量；Tentative仍需下游状态门控。

完整5400帧回放与上轮body-primary基线的检测、轨迹既有字段和选择逐帧完全相同。新增测量记录1220，历史4571，从未测量2008（逐帧全部轨迹计数，含Lost，非准确率）。视频完整解码并查看3993帧叠加。证据 artifacts/real-recordings-v1/head-measurement-v1/verification.json。CPU27/27、SDK6/6、Python33/33通过；增加ByteTrack低分阶段断言后，两套构建的CV/IMM测试再次通过。此前文档“测量有效性未完成”描述由本条更新；仍不证明头部检测正确、ReID或最终控制精度达标。
