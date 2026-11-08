# 运行前固定的本地工程门槛

1. 单进程退出码0，运行不少于3590秒，捕获108000帧，输出帧号/采集时间严格递增。
2. captured = processed + replaced + rejected + pending + stale + failed；pending、failed、rejected、stale均为0；peak≤capacity；latest替换比例≤1%。
3. 逐帧精确复算result-age P99≤33.334ms（当前30FPS单帧预算，仅采集完成到发布）；summary全程计数/最大值一致，近似分位数满足1%+2ns加JSON舍入容差。
4. 末五分钟private bytes中位数减第5～10分钟中位数≤32MiB；该门槛用于发现明显增长，单次通过不证明无泄漏。两个窗口均须有至少240个一秒采样。
5. 输出视频逐帧解码数量等于processed；源/程序/配置摘要和资源原始数据完整保存。

不通过时保留原输出，不改变门槛或删除异常帧。GPU整卡采样只能作环境背景，不据此归属进程显存。至少记录实际GPU型号及本机SDK条件。异机复现、全链路硬实时和多小时稳定性继续待验收。
