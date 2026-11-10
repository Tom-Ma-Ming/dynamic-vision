# SA02–SA04 套件

总体：UNKNOWN。这不是设备通过，也不是 TensorRT 通过。`passed` 为 false。

默认控制器是 limited_step。fixed_pid 只在模式被显式写明时运行，增益来自配置，没有自动调参。

| 场景 | 种子 | 控制器 | 后端 | 末段最大误差 px | 覆盖 | 总体 |
|---|---:|---|---|---:|---:|---|
| right | 11 | off | simulator | 20.0 | 0.0 | UNKNOWN |
| right | 11 | limited_step | simulator | 1.0 | 0.98 | UNKNOWN |
| right | 11 | fixed_pid | simulator | 0.0 | 0.9722222222222222 | UNKNOWN |
| right | 22 | off | simulator | 20.0 | 0.0 | UNKNOWN |
| right | 22 | limited_step | simulator | 1.0 | 0.9807692307692307 | UNKNOWN |
| right | 22 | fixed_pid | simulator | 1.0 | 0.9807692307692307 | UNKNOWN |
| right | 33 | off | simulator | 20.0 | 0.0 | UNKNOWN |
| right | 33 | limited_step | simulator | 0.0 | 0.9583333333333334 | UNKNOWN |
| right | 33 | fixed_pid | simulator | 1.0 | 0.9811320754716981 | UNKNOWN |
| left | 11 | off | simulator | 20.0 | 0.0 | UNKNOWN |
| left | 11 | limited_step | simulator | 1.0 | 0.9807692307692307 | UNKNOWN |
| left | 11 | fixed_pid | simulator | 0.0 | 0.9705882352941176 | UNKNOWN |
| left | 22 | off | simulator | 20.0 | 0.0 | UNKNOWN |
| left | 22 | limited_step | simulator | 1.0 | 0.9795918367346939 | UNKNOWN |
| left | 22 | fixed_pid | simulator | 1.0 | 0.98 | UNKNOWN |
| left | 33 | off | simulator | 20.0 | 0.0 | UNKNOWN |
| left | 33 | limited_step | simulator | 1.0 | 0.9795918367346939 | UNKNOWN |
| left | 33 | fixed_pid | simulator | 0.0 | 0.9714285714285714 | UNKNOWN |
| up | 11 | off | simulator | 20.0 | 0.0 | UNKNOWN |
| up | 11 | limited_step | simulator | 0.0 | 0.9583333333333334 | UNKNOWN |
| up | 11 | fixed_pid | simulator | 0.0 | 0.9655172413793104 | UNKNOWN |
| up | 22 | off | simulator | 20.0 | 0.0 | UNKNOWN |
| up | 22 | limited_step | simulator | 1.0 | 0.9807692307692307 | UNKNOWN |
| up | 22 | fixed_pid | simulator | 0.0 | 0.9696969696969697 | UNKNOWN |
| up | 33 | off | simulator | 20.0 | 0.0 | UNKNOWN |
| up | 33 | limited_step | simulator | 1.0 | 0.98 | UNKNOWN |
| up | 33 | fixed_pid | simulator | 0.0 | 0.9642857142857143 | UNKNOWN |
| down | 11 | off | simulator | 20.0 | 0.0 | UNKNOWN |
| down | 11 | limited_step | simulator | 1.0 | 0.9777777777777777 | UNKNOWN |
| down | 11 | fixed_pid | simulator | 1.0 | 0.9743589743589743 | UNKNOWN |
| down | 22 | off | simulator | 20.0 | 0.0 | UNKNOWN |
| down | 22 | limited_step | simulator | 0.0 | 0.9565217391304348 | UNKNOWN |
| down | 22 | fixed_pid | simulator | 0.0 | 0.9666666666666667 | UNKNOWN |
| down | 33 | off | simulator | 20.0 | 0.0 | UNKNOWN |
| down | 33 | limited_step | simulator | 1.0 | 0.9807692307692307 | UNKNOWN |
| down | 33 | fixed_pid | simulator | 1.0 | 0.9743589743589743 | UNKNOWN |
| diag-pp | 11 | off | simulator | 16.97056274847714 | 0.0 | UNKNOWN |
| diag-pp | 11 | limited_step | simulator | 0.0 | 0.9473684210526315 | UNKNOWN |
| diag-pp | 11 | fixed_pid | simulator | 0.0 | 0.9583333333333334 | UNKNOWN |
| diag-pp | 22 | off | simulator | 16.97056274847714 | 0.0 | UNKNOWN |
| diag-pp | 22 | limited_step | simulator | 0.0 | 0.9473684210526315 | UNKNOWN |
| diag-pp | 22 | fixed_pid | simulator | 0.0 | 0.9583333333333334 | UNKNOWN |
| diag-pp | 33 | off | simulator | 16.97056274847714 | 0.0 | UNKNOWN |
| diag-pp | 33 | limited_step | simulator | 0.0 | 0.9473684210526315 | UNKNOWN |
| diag-pp | 33 | fixed_pid | simulator | 0.0 | 0.9583333333333334 | UNKNOWN |
| diag-nn | 11 | off | simulator | 16.97056274847714 | 0.0 | UNKNOWN |
| diag-nn | 11 | limited_step | simulator | 0.0 | 0.9473684210526315 | UNKNOWN |
| diag-nn | 11 | fixed_pid | simulator | 0.0 | 0.9583333333333334 | UNKNOWN |
| diag-nn | 22 | off | simulator | 16.97056274847714 | 0.0 | UNKNOWN |
| diag-nn | 22 | limited_step | simulator | 0.0 | 0.9473684210526315 | UNKNOWN |
| diag-nn | 22 | fixed_pid | simulator | 0.0 | 0.9583333333333334 | UNKNOWN |
| diag-nn | 33 | off | simulator | 16.97056274847714 | 0.0 | UNKNOWN |
| diag-nn | 33 | limited_step | simulator | 0.0 | 0.9473684210526315 | UNKNOWN |
| diag-nn | 33 | fixed_pid | simulator | 0.0 | 0.9583333333333334 | UNKNOWN |
| diag-pn | 11 | off | simulator | 16.97056274847714 | 0.0 | UNKNOWN |
| diag-pn | 11 | limited_step | simulator | 0.0 | 0.9473684210526315 | UNKNOWN |
| diag-pn | 11 | fixed_pid | simulator | 0.0 | 0.9615384615384616 | UNKNOWN |
| diag-pn | 22 | off | simulator | 16.97056274847714 | 0.0 | UNKNOWN |
| diag-pn | 22 | limited_step | simulator | 0.0 | 0.9473684210526315 | UNKNOWN |
| diag-pn | 22 | fixed_pid | simulator | 0.0 | 0.9583333333333334 | UNKNOWN |
| diag-pn | 33 | off | simulator | 16.97056274847714 | 0.0 | UNKNOWN |
| diag-pn | 33 | limited_step | simulator | 0.0 | 0.9473684210526315 | UNKNOWN |
| diag-pn | 33 | fixed_pid | simulator | 0.0 | 0.9583333333333334 | UNKNOWN |
| diag-np | 11 | off | simulator | 16.97056274847714 | 0.0 | UNKNOWN |
| diag-np | 11 | limited_step | simulator | 0.0 | 0.9473684210526315 | UNKNOWN |
| diag-np | 11 | fixed_pid | simulator | 0.0 | 0.9583333333333334 | UNKNOWN |
| diag-np | 22 | off | simulator | 16.97056274847714 | 0.0 | UNKNOWN |
| diag-np | 22 | limited_step | simulator | 0.0 | 0.9473684210526315 | UNKNOWN |
| diag-np | 22 | fixed_pid | simulator | 0.0 | 0.9583333333333334 | UNKNOWN |
| diag-np | 33 | off | simulator | 16.97056274847714 | 0.0 | UNKNOWN |
| diag-np | 33 | limited_step | simulator | 0.0 | 0.9473684210526315 | UNKNOWN |
| diag-np | 33 | fixed_pid | simulator | 0.0 | 0.9583333333333334 | UNKNOWN |
| outside | 1 | off | simulator | 200.0 | 0.0 | UNKNOWN |
| outside | 1 | limited_step | simulator | 200.0 | 0.0 | UNKNOWN |
| outside | 1 | fixed_pid | simulator | 200.0 | 0.0 | UNKNOWN |
| between | 1 | off | simulator | 60.0 | 0.0 | UNKNOWN |
| between | 1 | limited_step | simulator | 60.0 | 0.0 | UNKNOWN |
| between | 1 | fixed_pid | simulator | 60.0 | 0.0 | UNKNOWN |
| held | 1 | off | simulator | 20.0 | 0.0 | UNKNOWN |
| held | 1 | limited_step | simulator | 12.0 | 0.9 | UNKNOWN |
| held | 1 | fixed_pid | simulator | 10.0 | 0.9 | UNKNOWN |
| release | 1 | off | simulator | 20.0 | 0.0 | UNKNOWN |
| release | 1 | limited_step | simulator | 1.0 | 0.9545454545454546 | UNKNOWN |
| release | 1 | fixed_pid | simulator | 1.0 | 0.9583333333333334 | UNKNOWN |
| rehold | 1 | limited_step | simulator | 12.0 | 0.9 | UNKNOWN |
| rehold | 1 | fixed_pid | simulator | 10.0 | 0.9 | UNKNOWN |
| pulse | 1 | limited_step | simulator | 1.0 | 0.9444444444444444 | UNKNOWN |
| walk-out | 1 | limited_step | simulator | 58.80000000000177 | 0.9875 | UNKNOWN |
| walk-out | 1 | fixed_pid | simulator | 58.400000000001796 | 0.9875 | UNKNOWN |
| identity | 1 | limited_step | simulator | 6.0 | 0.9333333333333333 | UNKNOWN |
| identity | 1 | fixed_pid | simulator | 7.0 | 0.9333333333333333 | UNKNOWN |
| mock | 1 | limited_step | mock_serial | 20.0 | 0.95 | UNKNOWN |
| dry | 1 | limited_step | dry_run | 20.0 | 0.95 | UNKNOWN |
| serial-refused | 1 | limited_step | serial | 20.0 | 0.875 | UNKNOWN |
| partial | 1 | limited_step | mock_serial | 20.0 | 0.9166666666666666 | UNKNOWN |
| send-fail | 1 | limited_step | mock_serial | 20.0 | 0.9166666666666666 | UNKNOWN |
| delay-0 | 1 | limited_step | simulator | 1.0 | 0.9523809523809523 | UNKNOWN |
| delay-0 | 1 | fixed_pid | simulator | 1.0 | 0.96 | UNKNOWN |
| delay-20 | 1 | limited_step | simulator | 1.0 | 0.9523809523809523 | UNKNOWN |
| delay-20 | 1 | fixed_pid | simulator | 1.0 | 0.96 | UNKNOWN |
| delay-50 | 1 | limited_step | simulator | 1.0 | 0.9545454545454546 | UNKNOWN |
| delay-50 | 1 | fixed_pid | simulator | 1.0 | 0.9615384615384616 | UNKNOWN |
| delay-100 | 1 | limited_step | simulator | 1.0 | 0.9565217391304348 | UNKNOWN |
| delay-100 | 1 | fixed_pid | simulator | 1.0 | 0.9583333333333334 | UNKNOWN |
| async-hold | 1 | limited_step | simulator | 18.0 | 0.6666666666666666 | UNKNOWN |

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

Windows 上补跑项目 Engine 的命令（本机未执行）：

```
cmake --build --preset windows-sdk --target vision_sim_image
out\windows-sdk\apps\trt_infer\Release\vision_sim_image.exe artifacts\engines\yolo11n-v002-retrain-20261110-fp32\best-fp32.engine <sprite> <background> configs\tracker-body-primary.yml <NEW_DIR> 19 42.4 431.2 674.9 1 1 0 0 0 0 human_led
```

同一条命令最后一个词换为 human_led_off 是关闭对照。该入口没有 fixed_pid 参数；本轮 PID 对照是 serial_assist run 的 CPU 仿真，不能把 vision_sim_image 说成已经跑过。
