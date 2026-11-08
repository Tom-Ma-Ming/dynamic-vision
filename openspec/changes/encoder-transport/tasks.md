# 状态及证据

- [x] 固定延迟、独立丢包、过期门控、有界在途/历史及记录字段。
- [x] 观察与实际收到的编码器配对；缺失观察不泄漏理想位移。
- [x] 单元验证未到达、丢失、过期、溢出、时钟回退；零延迟相同控制MAE。
- [x] 全丢和过期无命令；20帧黑屏后第22帧获得有效样本；反馈时间不超前。
- [x] CPU34/34；最后一次配对来源修改后control/sim定向2/2。
- [x] 四场景双跑五项产物逐字节一致，374条命令生命周期审计通过。
- [ ] 延迟控制精度：80ms延迟和恢复场景仍FAILED/timeout，最终误差2px/3px，未达原稳定门槛。
- [ ] 实际异步时钟、设备encoder及乱序/抖动。

入口configs/sim-encoder-feedback.yml；证据artifacts/sim/encoder-transport-v1及encoder-transport-repeat-v1。全丢/过期均UNKNOWN/control，不能把传输契约测试通过当作控制任务成功。
