# 实施清单（全部待验收）

SA01 本次输入与能力对照已记录在 docs/serial-protocol-baseline.md；当前以用户给出的 legacy_m_noack 源码为开发基线，实际烧录版本待设备测试前核实。SA02/SA05 必须分别测试无应答和可选应答模式，不要求用户寻找带击发命令的固件。

各项证据写入 artifacts/serial-assist/<commit>/<SA-ID>/<新run>/；实际运行命令由交付应用的 --help 和配置固定，尚未实现的入口不能算执行成功。

- [ ] SA01：冻结提交/依赖/协议与模型清单；读取旧serial_mouse.cpp及固件，完成能力表。
- [ ] SA02：include/vision/io及控制接口、src/io mock/dry_run，完整命令快照和生命周期测试。
- [ ] SA03：JSONL日志、manifest、独立审计及损坏日志负例；Python测试与CPU测试。
- [ ] SA04：现有TensorRT图像入口接项目Engine；off/limited_step/fixed_pid同源对照；固定与异步故障场景。
- [ ] SA05：Windows串口读写/编解码、部分写/应答/超时/取消/重连；假串口及真实协议证据分开。
- [ ] SA06：确认设备后在自建窗口标定位移与延迟，冻结标定文件和验收参数。
- [ ] SA07：闭环、故障和长时运行；CPU CI及本机TRT证据；测试启动/停止/审计包与独立复审。
