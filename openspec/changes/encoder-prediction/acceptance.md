# 验收规则

使用既有CPU全套，control_contract覆盖外推与饱和反例，sim_closed_loop覆盖全丢时仍禁发。关闭新开关保持原行为。原误差/稳定时间门槛不变。

sim-encoder-feedforward-ablation.yml：原模式、仅horizon0、仅外推+horizon0、仅前馈+horizon0、组合+horizon0；两速度两种子，比较末40帧误差。

sim-encoder-feedforward-dynamic.yml保留原24项动态参数只增加实验开关及显式horizon0；双跑标准产物相同，每条命令恰好一次终结。必须公开FAILED/UNKNOWN和成功锁存后又丢失目标的情况。实验改善只说明该条件下有效，动态全窗口稳定前不更改默认。
