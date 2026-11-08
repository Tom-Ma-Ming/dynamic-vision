# DeepSeek 实现与独立评审交接 · 2026-11-08

本文件是可用交接包，不代表已经有外部代理开始工作。当前仓库E:\mac-share\tensor\dynamic-vision，代码基线dab2ec6；本交接提交仅增加参数实验配置/文档。先阅读task-roadmap.md、encoder-gain-experiment.md、openspec/changes/encoder-transport以及stop-confirmation。旧handoff-grok.md保留历史，不能用其测试数量或完成项替代当前状态。

后续诊断补充：encoder-control-diagnosis.md已将失败分为无断轨时的持续运动偏差，以及边界/显式变向后的断轨。constant场景也会边界反弹，不能全部算作永远匀速。新增sim-encoder-straight-diagnosis.yml四用例保持ID1、末段ready仍全部FAILED，应分别定位，不要通过自动切换所选ID掩盖断轨。

## 下一项必须解决的工程问题

在现有严格门槛下修复编码器/视觉观测延迟下的控制误差，然后推进真实异步调度。先复现sim-encoder-transport.yml：2FAILED/2UNKNOWN；不要误报整体通过。显式Kp=2能解决静止用例，但动态对照24项仍3SUCCESS/17FAILED/4UNKNOWN，见encoder-gain-experiment.md。应按观测时间、已接收编码器时间、预测时间、正向命令时间逐段定位误差；分清视觉坐标系变化、目标速度和控制器运动。禁止在控制分支直接读取真值位置或尚未交付的编码器累计位移。

异步阶段须明确采集/推理/反馈/执行的时钟、发布顺序和有界队列；固定时间步模拟服务耗时不能称作实际异步运行。不可用控制观测必须门控；故障禁发、旧epoch/nonce拒绝、停止屏障和命令恰好一次终结均不得退化。新增行为独立开关，默认变更必须有对照证据。

## 开发隔离与资料

使用独立分支/工作树；不要两个代理同时写本目录，不要执行git clean或覆盖用户标注。当前用户另有.gitignore修改、四个docs/rec_*.avi、synth-training-poc及合成数据工具未入库，不属于本任务，不要一起提交或删除。artifacts大多不在Git中，新工作树没有它们；本次CPU合成用例无需复制录像/Engine，若做GPU实验应明确引用原只读数据和本地依赖。

远程CI按用户要求暂缓，不配置/推送远程。person95%仍未验收；ReID未实现，group_id只是局部轨迹编号，不能把不同编号直接当不同人物。不得改原标注去迎合指标。

## 可复现入口

本机CMake目录：F:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin；OpenCV DLL：F:\opencv\opencv\build\x64\vc16\bin。将它们加入当前会话PATH；不要修改全局配置。仓库已有windows-cpu preset。

```powershell
cmake --build --preset windows-cpu
ctest --preset windows-cpu --output-on-failure
./out/windows-cpu/apps/sim_runner/Release/sim_runner.exe configs/sim-encoder-transport.yml artifacts/your-new-baseline configs/tracker.yml
./out/windows-cpu/apps/sim_runner/Release/sim_runner.exe configs/sim-encoder-gain-holdout.yml artifacts/your-new-dynamic configs/tracker.yml
$env:PYTHONUTF8='1'
./.venv/Scripts/python tools/audit_sim_commands.py artifacts/your-new-dynamic/commands.jsonl
```

输出必须用新目录。新工作树需要自己的构建产物，可用主仓库.venv只读运行审计脚本；不要假设复制来的二进制对应新源码。

## 交回材料和裁判规则

1. 提交commit ID、设计说明、明确根因及坐标/时钟公式；注明未完成部分。
2. 保留修改前后全部场景、原始runs/commands/summary和可重跑命令；完整CPU回归，按改动补充非循环论证的单元测试。
3. 原门槛不变，不能删FAILED/UNKNOWN、延长窗口到碰巧成功、只报最后一帧或短暂成功。报告完整窗口、末段误差、覆盖、失踪/重获及故障禁发。
4. 动态24项是已见回归集，另设不同速度/噪声/种子/延迟验证；不得只在静止目标调参。若无法全面通过，给出反例和限制，不能标记主任务完成。
5. 我方独立checkout/build/run，检查真值泄漏、时间倒流、旧命令执行、默认回归和结果归因，再决定接受或退回。不得只引用开发者的“测试通过”摘要。

完整项目仍有ReID、可靠身份映射、代表性检测95%、复杂镜头补偿、部署/长时和异步链路验收缺口，本交接不缩小总目标。
