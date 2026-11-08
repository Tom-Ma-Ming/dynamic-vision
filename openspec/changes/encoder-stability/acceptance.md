# 验收

原 epsilon、settle_frames 和场景窗口不变。SUCCESS 计数可以和以前相同，但必须同时报告 success_maintained 与 end_stable。动态 24 项本轮是 5/15/4，后两个合计都是 0，因此不接受为控制任务完成。

传输基线必须仍是 2 FAILED / 2 UNKNOWN，不能写成整组通过。机动加速度对照可以保留 ID，但若严格成功和结束稳定没有提高，不能改成默认跟踪器。

复算命令：

```powershell
$env:PYTHONUTF8='1'
./.venv/Scripts/python tools/audit_sim_stability.py artifacts/sim/encoder-feedforward-stable-v1
```
