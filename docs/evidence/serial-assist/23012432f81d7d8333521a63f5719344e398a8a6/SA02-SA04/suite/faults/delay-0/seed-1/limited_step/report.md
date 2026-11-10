# delay-0 / limited_step / simulator

总体：UNKNOWN。`summary.passed` 为 false。UNKNOWN 不是通过。

- 协议：legacy_m_noack，115200 8N1，ack/sequence/stop/execution_feedback 均为 false。
- 主机终态和设备结果分开。无 ACK 时写入完整的设备结果是 UNKNOWN，applied_ns 保持 null。
- M 0 0 不是 STOP。本轮不发送点击/击发，不打开真实串口，不扫描端口。
- 人工轨迹 SHA-256：`bb5de16763dfe2d294ec75fcad9b497f5ed9d8fc3e6dc379601fe523ab44f39b`
- 末段最大真值误差 px：1
- 资格覆盖：0.952381
- 辅助命令数：10，hold 期间新辅助：0，区外输出：0

## 失败

无主机硬门槛失败。

## UNKNOWN

- flashed_firmware_identity
- flashed_firmware_matches_source
- device_model
- com_port
- end_to_end_latency_ms
- device_counts_per_px
- usb_hid_queue_depth
- partial_write_recovery
- device_watchdog
- ok_semantics_on_device
- device_execution_confirmation
- remote_stop_confirmation
- tensorrt_execution

源码基线不证明当前烧录固件。TensorRT 不在本次进程内执行。
