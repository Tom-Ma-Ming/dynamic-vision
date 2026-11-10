# SA01–SA04 交回 · 2026-11-10

代码提交 `23012432f81d7d8333521a63f5719344e398a8a6`。总体 **UNKNOWN**，`passed` 为 false。UNKNOWN 没有计为通过。Codex 尚未复审。SA05–SA07 未开始。

`start-preview`、`start-simulation`、`start-serial-test`、`stop` 没有实现，调用会退出 2 并说明未实现。不要把它们当成可运行入口。

## 协议

开发基线是 `legacy_m_noack`：115200、8N1、`ack=false`、`sequence=false`、`stop=false`、`execution_feedback=false`、只发送 `M dx dy`。单位是设备 counts，单轴单段 -127..127。`M 0 0` 不是 STOP。源码哈希不证明当前烧录固件。主机写入完整只记 `write_completed`，设备结果保持 UNKNOWN，`applied_ns` 保持 null。没有打开串口，没有扫描端口，没有烧录，没有自动调 PID。

## 命令和退出码

Linux 上没有 Visual Studio，未跑 `cmake --preset windows-cpu`。等价 CPU 配置：

| 命令 | 退出码 | 结果 |
|---|---:|---|
| `CXX=g++ cmake -S . -B out/linux-cpu -DVISION_PROBE_ONLY=OFF -DVISION_WITH_OPENCV=OFF -DVISION_WITH_TRT=OFF -DVISION_BUILD_TESTS=ON` | 0 | 生成构建 |
| `cmake --build out/linux-cpu --target serial_assist assist_test vision_tests control_test env_probe` | 0 | 编过 |
| `ctest --test-dir out/linux-cpu --output-on-failure` | 0 | 29/29。`probe_environment` 在无 TensorRT 时被标为 WILL_FAIL，CTest 记通过只表示探针按预期失败，不是 TensorRT 通过 |
| `python3 -m pytest tests/tools/test_audit_serial_assist.py -q` | 0 | 6 passed。覆盖损坏行、缺帧、重复终结、伪成功、hold 期间输出 |
| `python3 tools/run_serial_assist_batch.py --root . --binary out/linux-cpu/serial_assist --out docs/evidence/serial-assist/23012432f81d7d8333521a63f5719344e398a8a6` | 2 | 批次完成，总体 UNKNOWN |

批次内部：

| 命令 | 退出码 |
|---|---:|
| `serial_assist inventory ... --dirty 0` | 0（清单写入；summary.passed 仍为 false） |
| `serial_assist run ...` | 2 |
| `python3 tools/audit_serial_assist.py <suite> --tree ...` | 2 |
| `python3 tools/probe_serial_model.py ...` | 2 |

## 证据

`docs/evidence/serial-assist/23012432f81d7d8333521a63f5719344e398a8a6/`

- SA01/inventory：manifest、能力表里的未知字段、空的运动日志
- SA02-SA04/suite：105 组运行，每组有 manifest.json、frames.jsonl、commands.jsonl、transport.jsonl、controller.jsonl、summary.json、report.md
- SA03/audit-summary.json：独立重算，失败列表为空，状态 UNKNOWN
- SA04/model-probe：CPU ONNX 与 TensorRT 状态

同一人工轨迹、种子 11，末 25 帧最大真值误差：

| 场景 | off | limited_step | fixed_pid |
|---|---:|---:|---:|
| right | 20 px | 1 px | 0 px |
| left | 20 px | 1 px | 0 px |
| up | 20 px | 0 px | 0 px |
| down | 20 px | 1 px | 1 px |

八个方向、种子 11/22/33 的区内实验门槛通过。默认控制器仍是 limited_step。fixed_pid 的 P 项有记录，limited_step 的 P/I/D 保持 0。延迟 20/50/100 ms、走动目标和真实设备没有套用 2.5 px 门槛。

## 模型

CPU Execution Provider 跑了仓库里的 COCO `models/yolo11n-coco-320.onnx`（SHA-256 `8726dd29e8439519c8986d830d5d732fe483ab8ab90cbb965f57fa1b8b97c505`），输出形状 1x84x2100，`project_contract=false`。项目 `best.onnx` 和 `best-fp32.engine` 不在这个检出里，状态 MISSING。本机没有 `/dev/nvidia0`。TensorRT 是 **blocked / UNKNOWN**。

未执行的 Windows 命令：

```text
cmake --build --preset windows-sdk --target vision_sim_image
out\windows-sdk\apps\trt_infer\Release\vision_sim_image.exe artifacts\engines\yolo11n-v002-retrain-20261110-fp32\best-fp32.engine <sprite> <background> configs\tracker-body-primary.yml <NEW_DIR> 19 42.4 431.2 674.9 1 1 0 0 0 0 human_led
```

把最后一个词换成 `human_led_off` 才是这条旧入口的关闭对照。它没有 fixed_pid 参数。本轮 PID 对照是 `serial_assist run`。

## 失败与 UNKNOWN

主机硬门槛和冻结的静止合成门槛没有失败项。以下保持 UNKNOWN，不能改成通过：

- 当前烧录固件身份，以及它是否等于源码哈希
- 设备型号、COM 端口
- 端到端延迟
- 设备 counts/px 标定
- USB/HID 队列深度
- 半包恢复
- 设备看门狗
- 设备上的 OK 语义
- 设备执行确认、远端停止确认
- TensorRT 执行
- 运动目标误差、真实设备误差、延迟门槛、接管停止 P95/P99、残余位移上限
