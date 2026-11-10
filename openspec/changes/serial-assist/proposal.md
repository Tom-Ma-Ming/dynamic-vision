# 有限微调串口接入

用户要求把现有 head/person TensorRT 识别与人主导微调接到旧串口执行流程，并用日志调参。范围、阶段与门槛以 docs/serial-assist-implementation-plan.md 为准。复用协议知识，不照搬旧类别/坐标/PID线程逻辑；仅移动，自建仿真和测试窗口优先。不包括在线自适应学习或点击击发。

影响 Simulator、控制接口、串口适配、配置、应用入口和独立审计。设备与固件信息待 SA01 核实；默认 mock/dry_run。
