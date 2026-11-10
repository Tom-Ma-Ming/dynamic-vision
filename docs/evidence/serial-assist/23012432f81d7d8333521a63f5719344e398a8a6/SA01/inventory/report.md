# SA01 清单

开发协议是 legacy_m_noack。波特率 115200，8N1，无 ACK、无序号、无 STOP、无执行回传，只发送 M。移动单位是设备 counts，单轴单段 -127..127。这些来自源码哈希，不证明设备当前烧录版本。

总体 UNKNOWN。未知项没有计为通过。真实串口未打开。

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
