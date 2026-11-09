# DeepSeek 当前工作树独立评审 · 2026-11-09

结论：部分实现和基础测试通过，但不能接受为任务完成；需要修改后重审。没有改动被评审的算法代码，也未替开发者修复。评审基线为HEAD 66cecd7加当前未提交diff及未跟踪StageScheduler.hpp；patch与源码/程序摘要已保存到artifacts/review-deepseek-20261109。

## 阻断验收的问题

### P1：延迟补偿无条件禁用垂直控制

src/sim/simulator.cpp:719、730：hold_y只看delay_mode和未显式开启velocity_feedforward，直接uy=0，既不检查目标垂直速度，也不检查垂直位置误差。注释把垂直运动视为检测噪声，只适用于本次水平场景，不能作为通用控制。

独立反例：start_x320、start_y120、vx=vy=0、噪声0、Kp2、编码器延迟80ms。关闭新补偿时SUCCESS、最终误差0px、40条非零垂直命令；默认补偿开启时FAILED、最终误差120px、非零垂直命令0。垂直移动20px/s同样全程没有垂直命令。证据vertical.yml和vertical/。应保留二维误差控制，用可验证的滤波/噪声模型处理噪声，并补垂直偏移、斜向和蹲起回归。

### P1：控制端账目提前读取设备实际执行状态

src/sim/simulator.cpp:235在dropFeedback及feedback_channel交付之前调用settleFlight；其处理applied/replaced/expired等物理事件，立即扣减/删除feedback_parts。随后控制器用该表的fb_x/fb_y作为pending。即使回传延迟或丢失，控制端也已经知道设备实际执行了多少。这绕过已有FeedbackReceiver的累计快照、epoch、序号与丢包模型，违反交接中的因果约束。应只在已收到的合法反馈上更新控制账目，并测试“相同已接收信息下，隐藏设备状态不同不能提前影响命令”。

### P1：通过降低/绕开不确定性门控改善分数

src/sim/simulator.cpp:139后自动启用96px机动恢复并把accel压到10；:641以观测融合时刻的协方差替代当前控制时刻的预测协方差。src/tracking/tracker.cpp:270恢复后直接把位置协方差重置为sigma²，其注释明确为避免控制门拒绝。数值阈值虽然没改，但输入阈值的量和协方差估计已变化，不能称为原门控下的同等验收。更大预测不确定性不应仅因影响成绩就被忽略；需有自洽的当前时刻估计协方差和误差覆盖验证。

### P1：异步模式中的捕获、执行与帧证据不在同一时刻

src/sim/simulator.cpp:469先计算truth位置并生成观测，:564才排入Capture事件；随后处理同刻Execution修改camera_x。帧日志在:798附近写入新的camera_x，却保留先前truth_x。Capture事件本身没有执行采集/推理，图像检测仍在固定循环内同步完成。

独立反例：静止世界目标start_x120、视角反馈、观测延迟50ms。旧模式所有帧truth_x+camera_x=120；async_stages开启后39帧违反该不变量，最大16px。第6帧truth_x120、camera_x=-16，重建世界位置变104。两个场景都锁存SUCCESS，说明现有测试没有发现数据时刻错误。证据async.yml和async/。应让物理推进、采样、推理结果发布及控制决策真正按事件时刻执行，原始样本与状态快照标明各自时钟，不能先做完工作再记录Capture事件。

此外：encoder_channel.advance(ts_ns)在排空更早事件之前执行，历史事件能访问已经推进到较晚时刻的接收状态；反馈事件仍每帧调度。当前是仿真事件队列，并非壁钟异步线程，文档对此已有承认；完整异步任务仍未完成。

### P1：默认行为与实验边界发生变化

include/vision/sim/Simulator.hpp:34将encoder_delay_compensation设为true；旧延迟配置无须显式启用便自动改变跟踪噪声、恢复半径、前馈、预测时域、协方差门控、垂直轴与亚像素增益。这不满足交接要求的独立默认关闭实验。tracker.yml文件没变不代表有效tracker参数没变。应显式配置/记录实际生效参数，并提供默认关闭兼容对照。

### P2：调度器状态并非全程有界，交付也尚未入库

StageScheduler.hpp:76的captured_按最大frame增长且不回收；每阶段事件队列虽有容量，但帧完成元数据随运行时长增长。enqueue还缺少时间/帧合法性及已处理时钟回退检查。应使用有界依赖状态、拒绝不合法的倒序事件，并记录拒绝/积压的生命周期。

StageScheduler.hpp仍未跟踪，调度和控制改动均未提交。当前HEAD只包含持续稳定性和时钟记录；无法用一个交回commit完整重建此次被评审实现。不要把本地未提交的通过结果当作可交付检查点。

## 已独立验证的有效进展

- 当前代码重新构建成功，CPU CTest34/34、已跟踪Python测试62/62通过。
- schema3正确区分锁存SUCCESS、success_maintained和end_stable，独立审计能复算。
- 原holdout24项确实为23SUCCESS/1FAILED/0UNKNOWN，但持续稳定仅8、结束稳定15。
- 显式前馈24项确实为10SUCCESS/14FAILED/0UNKNOWN，持续稳定6、结束稳定3。
- 机动恢复与阶段调度已有代码和基础测试，但不能从单目标“保留同ID”测试证明多人身份正确；单个附近检测也可能是不同人物。

证据来自本次新输出目录holdout/、feedforward/，均运行audit_sim_stability.py。上述数量认可为当前仿真输出，不认可为泛化控制能力证明；垂直禁用、反馈真值提前使用和门控变化需要修复后重新测量。

## 尚未完成及交回要求

1. 二维/斜向控制、原动态24项的持续稳定，以及新的速度/噪声/种子独立验证。
2. 严格反馈因果、当前控制时刻不确定性、异步采样/执行/发布的时间一致性；壁钟异步任务仍待实现。
3. 多目标误匹配/错误重获反例，不能以ID不变代替身份保持；ReID仍未实现。
4. 原项目person95%、可靠身份映射、代表性录像与负样本、复杂镜头补偿、实际部署和综合验收仍缺。远程CI按用户要求暂缓，不计作开发者擅自遗漏。
5. 独立分支提交完整实现和依赖头文件；给出源代码、配置、二进制摘要及原始输出，再按上述新增反例重审。

本轮建议退回修复前五项P1，保留已经正确实现的持续稳定性统计。评审结论不是“全部无效”，也不是“全部完成”。

## 固定反例的自动复验

新增configs/sim-review-vertical.yml、sim-review-async.yml和tools/verify_sim_review.py，不修改算法。检查器使用新输出目录，运行两组用例，独立核对稳定计数/命令账目、完整帧序列、垂直动作和静止世界坐标一致性。程序与配置运行前后摘要必须一致，原始输出和verification.json保留。返回码1表示验收失败，不是必须修改检查器的脚本故障；不能通过关闭async_stages或encoder_delay_compensation修改固定反例来过关。

```powershell
$env:PATH = "F:\opencv\opencv\build\x64\vc16\bin;" + $env:PATH
$env:PYTHONUTF8 = '1'
./.venv/Scripts/python tools/verify_sim_review.py out/windows-cpu/apps/sim_runner/Release/sim_runner.exe artifacts/your-new-review-run
```

当前检查明确失败：旧补偿关闭的垂直偏移和固定时钟对照通过，新补偿的两个垂直用例不产生纵向命令，async1出现39帧坐标不一致、最大16px。这套检查只覆盖本轮两个可执行反例；即使后续全部通过，反馈因果、协方差自洽、多人身份恢复、真正异步链路和总体路线图仍需单独验收。

## 阶段时钟防御性检查补充

独立编译tests/review/stage_scheduler_probe.cpp直接调用当前StageScheduler，确认三项均未防护：

1. Execution时刻100已出队后，再入队Execution50仍成功，随后出队50，阶段时钟倒退。
2. Inference10等待Capture20，捕获出队后返回Inference10，依赖解除造成全局事件时间倒退。
3. Execution时间-1未拒绝。

这说明调度器没有拒绝无效输入/过期事件的时钟契约；不是声称所有正常配置都会触发。测试不修改现有算法，返回1表示反例未修复。依赖时刻冲突应拒绝或明确修正且记录，不能把较早的计划时刻直接当作较晚实际处理的时间。该探针只验证不倒退和负时间拒绝，不替代队列活性/公平性/有界内存验收。

```powershell
cmake -S tests/review -B out/review-stage-clock -G "Visual Studio 16 2019" -A x64
cmake --build out/review-stage-clock --config Release
./out/review-stage-clock/Release/stage_scheduler_probe.exe
```

本机实测输出execution_clock=100 -> 50、dependency_clock=20 -> 10，三个检查均false，退出码1。构建只依赖C++17及待评审的StageScheduler.hpp，仍须将该头文件纳入交付。
