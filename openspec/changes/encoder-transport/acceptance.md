# 验收

control_contract验证延迟到达、不可用、过期、容量溢出与时间回退。sim_closed_loop验证零延迟与理想模式MAE相等、全丢/过期不生成命令、恢复前禁发及接收因果关系。完整CPU套件通过。

sim-encoder-transport.yml双跑，所有标准产物一致；audit_sim_commands检查每条命令恰好一次终结。日志encoder_sample_ts_ns不超过当前时间减延迟，在途不超过256，样本不可用时不生成动作。

控制精度继续沿用原场景稳定窗口和误差阈值；不能因为门控正确就宣称精度通过。当前两个延迟场景FAILED，作为下一步优化基线。
