# 验收

以 docs/serial-protocol-baseline.md 区分能力：noack模式必须无ACK等待死锁、无伪应答/执行确认、无不支持指令发送；ACK碎片/迟到测试只测ack分支。设备停止确认在当前固件下为not_supported，不能计为通过；主机停止产生新命令与视觉残余评估仍须验证。写入完整可形成主机终态，不代表设备已执行。

以 docs/serial-assist-implementation-plan.md 验收表为准。先冻结 acceptance-profile.json，未知阈值标 UNKNOWN。硬门槛不得放宽；合成实验数值不是产品通用承诺。

Given 相同输入和配置，When 开/关辅助或切换控制器，Then 人工输入完全一致、日志完整可复算，区外/hold零新辅助，区内双轴有效且满足限幅；长hold无释放不恢复；旧epoch应答不授权动作。

Given 部分写/100%发送失败/无应答/异步停止，When 发送或取消，Then 字节/状态账目正确、未执行不得记录applied、停止与到期不依赖未来普通命令、有界退出。

命令基线：cmake --build --preset windows-cpu；ctest --preset windows-cpu --output-on-failure；使用项目Python运行新增审计测试。TRT/设备运行必须提供实际参数与输出，缺GPU或设备时该项 UNKNOWN，不得用mock替代。原始输入及日志保留，独立审计不能只信summary的passed。
