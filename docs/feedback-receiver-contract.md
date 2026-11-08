# 有序反馈接收与仿真传输契约

`FeedbackReceiver` 已接入默认关闭的仿真命令反馈传输层（feedback_transport=1）。未启用时仍使用原同步CommandFeedback路径；真实设备未接入。停止确认仍走独立零延迟模型，不能把命令通知的延迟乱序验证扩展为完整设备恢复协议。

消息必须带epoch、command_id、递增sequence、reported、accepted_at、累计applied_x/y和terminal。时间已换算到控制器仿真时钟；真实设备时钟同步尚未实现。reported不可晚于本地接收时刻或早于命令生成/上次报告；首次接受时刻必须位于生成和报告之间，后续快照不得更改。延迟估计使用原accepted_at，不把回传等待时间误当作正向执行延迟。

接收顺序规则：

- 同命令重复/较小序号不再更新账目。中间消息丢失后，较新累计执行量可一次补齐差值。
- terminal清理命令；后到旧消息返回Unknown，不重建账目。同一epoch不允许重新使用旧命令ID。
- 接收更小epoch返回Stale，更大epoch未经本地同步不得接受；显式beginEpoch只能递增epoch，且时间不能回退。调用方必须已确认停止并清空旧传输，beginEpoch本身不验证硬件状态。
- 非有限执行量、错误接受时刻、接收时钟回退、未来报告拒绝更新。每个接收器默认最多512条未终结命令，超额issued明确失败，不以无限增长掩盖反馈缺失。

测试覆盖跳过序号补齐、重复/乱序不重复扣减、终结后迟到、跨epoch同ID隔离、容量拒绝、NaN及时间异常，control_contract通过。下一步需把实际事件转换为累计快照，接入有界传输队列/延迟抖动/丢失，再核验端到端账目、停止确认与恢复；旧停止快照的请求nonce及重放保护仍未实现。

## 有界命令反馈通道 · 2026-11-08

feedback_transport启用时要求incremental_control和正的feedback_timeout_ms。feedback_delay_ms/feedback_jitter_ms范围0～10000，反馈传输容量feedback_capacity范围1～10000。实际事件先更新累计快照，再按丢包模型决定是否排队；回传延迟为max(0,delay+均匀抖动)，按ready排序，在仿真帧调度点交付。多条消息同帧交付时仍按ready稳定排序。消息reported/ready/received分别记录事件报告、可交付和实际接收时刻。

队列溢出丢弃最早报告的待交付快照，并记录overflow；较新的累计快照可以补齐之前的动作，但丢失终结且长期未确认仍会触发故障。接收器最多512条未终结命令，达到上限进入feedback_capacity控制故障；期限超时仍为feedback_timeout。故障/成功重新同步时递增epoch，保留在途旧消息以验证旧epoch拒绝。默认关闭行为与前一版恢复场景一致。

新增feedback.jsonl逐消息证据；status为applied（接收器已接受，不表示此刻才物理执行）、stale、unknown、invalid、dropped、overflow或pending。pending仅代表场景窗口结束时尚未交付。消息队列与账目有界；全场景trace与既有commands/frames一样保存有限实验历史，不代表运行时日志本身已环形化。

configs/sim-feedback-transport.yml四场景双跑逐字节一致：

| 场景 | 结果 | 主要消息状态 | 最大在途/未终结 |
|---|---|---|---|
| 60ms延迟±80ms抖动 | SUCCESS | 接受357、unknown56、stale2 | 8/7 |
| 容量1、300ms延迟±80ms | UNKNOWN/control | overflow66、stale1 | 1/25 |
| 延迟500ms、期限200ms、恢复请求 | UNKNOWN/control | 旧epoch stale20 | 10/5 |
| 图像投影反馈及预测，60ms±80ms | SUCCESS | 接受350、unknown60、stale2 | 8/7 |

最后一个场景使用合成观测和理想encoder，不是TensorRT图像或真实设备验收。unknown是终结/本地取消之后的迟到命令消息，未重建账目。四场景invalid均为0，接受序号严格递增，reported≤ready≤received；回传抖动未改变80ms正向延迟估计。433条命令生命周期审计通过。证据 artifacts/sim/feedback-transport-v1、feedback-transport-repeat-v1及verification.json。关闭新通道后原恢复场景的命令和既有帧字段一致。

CPU34/34通过；之后新增512账目容量故障断言及针对性control/sim复测通过。真实回传时钟换算、停止确认请求nonce、延迟旧停止快照和encoder反馈丢失仍待实现。
