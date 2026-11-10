# 实施清单（全部待验收）

SA01 本次输入与能力对照已记录在 docs/serial-protocol-baseline.md；当前以用户给出的 legacy_m_noack 源码为开发基线，实际烧录版本待设备测试前核实。SA02/SA05 必须分别测试无应答和可选应答模式，不要求用户寻找带击发命令的固件。

各项证据写入 artifacts/serial-assist/<commit>/<SA-ID>/<新run>/；实际运行命令由交付应用的 --help 和配置固定，尚未实现的入口不能算执行成功。

- [x] SA01：冻结提交/依赖/协议与模型清单；以源码哈希完成能力表。证据 docs/evidence/serial-assist/23012432f81d7d8333521a63f5719344e398a8a6/SA01。总体 UNKNOWN，不是设备通过。Codex 尚未复审。
- [x] SA02：include/vision/io 与控制接口，simulator/mock_serial/dry_run/serial。串口保持关闭。证据在同一提交的 SA02-SA04/suite/backends。
- [x] SA03：JSONL、manifest、独立审计及损坏/缺帧/重复终结/伪成功负例。审计总体 UNKNOWN，失败项为空。
- [x] SA04：同源人工轨迹 off/limited_step/fixed_pid。CPU COCO ONNX 已执行且不是项目契约。TensorRT blocked/UNKNOWN。没有把缺 GPU 记成通过。
- [ ] SA05：Windows串口读写/编解码、部分写/应答/超时/取消/重连；假串口及真实协议证据分开。
- [ ] SA06：确认设备后在自建窗口标定位移与延迟，冻结标定文件和验收参数。
- [ ] SA07：闭环、故障和长时运行；CPU CI及本机TRT证据；测试启动/停止/审计包与独立复审。
