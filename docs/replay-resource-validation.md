# 回放资源与长时验收

回放原先为每一帧保留queue/infer/MOT/result-age（可选camera）耗时，内存随运行长度增长。现改用全程固定大小的对数直方图，每个统计对象小于40KB，五项合计小于200KB；不保留统计用的逐帧vector。原始逐帧耗时继续写入frames.jsonl，磁盘输出仍随时间增长，实际部署还需日志轮转/配额。

summary.json 的 `latency_quantiles=log_histogram_upper_bound_1pct_plus_2ns` 明确新口径：P50/P95/P99为全程最近秩分位数所在桶上界，超估不超过1%+2ns（JSON文本序列化另有浮点舍入）；样本数和最大值保持全程统计，分位值不超过最大值。这不是滚动窗口，也不是只抽取最后若干帧。离线精确统计函数保留用于测试与核验。

CPU新增测试用百万样本（含多秒尾部）、负值、零样本和int64极值验证计数、最大值、误差上界和固定内存尺寸。仅证明统计模块，不等于整个进程无内存泄漏。

Windows监测工具按秒记录子进程working set、private bytes、累计CPU秒数和句柄数，输入文件/程序SHA256写入manifest。每个输出目录只能创建一次；超时只终止本工具启动的子进程。CPU字段是累计CPU时间，不是百分比。退出结果保留completion.json、stdout/stderr，原始回放放在replay子目录。

```powershell
& tools/monitor_replay.ps1 -Exe out/windows-sdk/apps/trt_infer/Release/vision_replay.exe -Engine artifacts/engines/yolo11n-v002-fp32/best-fp32.engine -Video docs/rec_20261107_202107.avi -Tracker configs/tracker.yml -Config configs/replay-realtime.yml -Output 新输出目录 -TimeoutSeconds 300
```

按image-replay.md配置OpenCV/TensorRT DLL路径。该命令按视频节奏运行三分钟，不是小时级老化测试。GPU采样须标明整卡统计与进程统计的区别，不能把同机其他程序的显存算作本进程占用。

## 2026-11-08 三分钟基线

证据 artifacts/replay-soak-v1：真实时间180.52秒，5400帧捕获/处理/视频解码一致，替换、拒绝、失败、积压均为0。精确复算P99：queue0.0671ms、infer13.4296ms、MOT0.0185ms、result-age13.4531ms；在线直方图分位数误差验证通过。result-age是采集完成到结果发布，不能替代包含采集/显示/执行的全链路延迟。

179次进程采样；30～60秒私有内存中位数271.586MiB，150秒后273.113MiB（+1.527MiB）；全程私有内存峰值307.879MiB、工作集峰值159.176MiB。只有三分钟，不能据此证明长期无增长。GPU独立采样见gpu-device.csv，属于整张GTX1060统计，不能当作本进程显存。CPU29/29、SDK6/6通过；监测工具实际运行完成。小时级压力、日志轮转、部署机器验证仍未完成。

## 一小时验收入口

运行前门槛见 openspec/changes/replay-soak/acceptance.md。make_soak_video.py生成20次重复的108000帧输入并保存来源清单；单进程monitor_replay运行结束后执行 `python tools/audit_replay_soak.py 输出目录`，独立检查输入摘要、帧账目、精确延迟、内存窗口和视频解码数量。三分钟基线作为反例送入小时审计器，按预期因时长/帧数/内存窗口不足返回失败；其audit.json不表示此前三分钟基线本身运行失败。

2026-11-08 已启动 artifacts/replay-soak-hour-v1，一小时结果尚未获得。输入 artifacts/replay-soak-input-v1，108000帧/3600秒；运行句柄83707（本会话exec session）。必须重新查询该句柄或实际进程确认状态，不能因本条启动记录宣称运行仍活跃或验收通过。在该进程结束前勿重新构建/覆盖vision_replay.exe；结束后自动执行独立审计。

## 一小时结果：本地门槛通过

artifacts/replay-soak-hour-v1/completion.json记录3602.379秒、退出码0，audit.json所有预设门槛通过。捕获108000、处理107974、latest替换26（约0.0241%），拒绝/积压/陈旧/失败均0，队列峰值1；输出视频解码107974帧。原始逐帧精确P99：排队0.072ms、推理13.98ms、MOT0.0178ms、结果年龄14.137ms。两段内存窗口各298样本，末五分钟私有内存中位数较第5～10分钟增加7.029MiB，小于预设32MiB门槛。

这是本机GTX1060/FP32/320输入、同一录像重复20次的单进程资源测试。期间同机有CPU开发/测试负载，无其他新GPU推理任务；不是空载独占性能测量。最大结果年龄98.223ms，虽然P99通过单帧预算，仍不能宣称逐帧硬实时或无延迟尖峰。内存仍有增长，单次一小时通过不证明多小时无泄漏。更长运行、实际部署机和完整视觉执行时延继续待验收。

上述小时测试不包含后续日志轮转源码；日志轮转使用独立程序单独完成GPU对照，见replay-log-retention.md。旧“正在运行”段落保留为阶段记录，当前对应进程已正常退出。
