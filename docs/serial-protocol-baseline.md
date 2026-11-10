# 串口协议源码基线 · 2026-11-10

已读取用户指定文件：
- E:/mac-share/mouse_firmware/mouse_firmware.ino
- E:/mac-share/tensorRT_Pro-main/tensorRT_Pro-main/src/application/auto_aim/serial_mouse.cpp

注意：本次主机路径不同于此前 auto_ai 参考目录。以这两个文件进行 SA01 对照，不假设它们与其他副本一致。源码基线不证明设备当前烧录版本；实际连接前核实设备/固件。

## 已确认能力

固件 Serial.begin(115200)，换行分包，识别 L/R、LD/LU、RD/RU、M dx dy、W n。没有 Serial.print/println 应答、序号、查询握手、STOP、CFG、F、S、A 或看门狗。主机的 M 移动格式匹配；probe_fire 等待 CFG 的 OK 不适用于本固件。RD/RU 未见对应主机接口，本轮只移植移动，不补点击功能。

moveRel 每轴每段限制 -127..127，每段 Mouse.move 后 delay(2)，循环结束前不再处理新串口命令。2ms 是固件等待，不是完整端到端执行延迟；不包含USB/系统/画面刷新。未知指令静默忽略，不能用沉默确认设备健康。

行缓冲最多保留40字符，超长后丢弃剩余字符但仍在换行时执行已保存前缀。toInt 不提供严格格式校验。因此主机必须在发送前拒绝非法/超长行，不能依赖固件报错；部分写超时后不得随意补换行或发送下一个运动包，以免触发残缺前缀。需要明确恢复方案或升级固件，未解决前该异常路径 UNKNOWN。

## 本协议配置（开发基线，不是设备已识别结果）

建议 profile=legacy_m_noack，baud=115200，host_serial=8N1，ack=false，sequence=false，stop=false，execution_feedback=false，supported_motion=M。

- 主机完整写入只记 write_completed；ACK、设备完成时间为 null/not_supported。不等待OK，不运行probe_fire，不因没有应答判定ACK超时，也不自动宣称执行成功。
- 主机command_id/epoch仍用于日志，但不发送为固件不认识的额外字段，不能宣称设备去重。
- 无ACK模式不能用“等待一个ACK才发下条”的策略。采用有界主机待发队列、经测试窗口标定的发送节奏和背压；写完后设备待执行数不可直接观测。时间预算只能作为估计，不算执行确认。
- 单包主机设备增量每轴不超过127 counts，并受更严格的已标定微调上限约束；127不是辅助允许步长，也不是px。避免触发固件多段长时间阻塞。不靠堆积小包绕过总速度/区域限制。
- 人工接管立即停止产生新辅助并清理尚未写出的包；已经进入OS/USB/固件的数据不保证可撤回。没有设备STOP，M 0 0不是STOP，关闭COM也不等于已执行动作被取消。
- 画面反馈可估计运动效果与延迟；标注 vision_estimated，不能在人工运动/多命令混合时强行归因某个command_id。视觉观测超时进入停辅助/未知。
- 若验收要求远端停止确认、精确设备队列界限或每条命令执行确认，这版固件无法提供。保留对应项 UNKNOWN/不支持，不把其他硬门槛改成通过。需要另一项显式固件升级任务，不能在此静默加命令或刷机。

## 主机代码独立问题

send() 仅判断 WriteFile 布尔返回，未检查 written==data.size()；可能将部分写入报成成功。后台读线程丢弃读入内容，即使以后接入有ACK固件，也需要统一解析/分发，不能让probe/readline与后台线程争抢应答。以上不是修改固件即可自动解决的问题。

## 对OpenSpec的修订

SA01 先交该profile与协议字节级测试；SA02/SA05 的 mock 同时覆盖 noack 和可选 ack 两种能力，实际默认使用已提供源码的 noack profile。ACK碎片/迟到/重复测试属于 ack 模式；noack 测试应证明无等待死锁、无伪ACK及无伪执行确认。命令生命周期拆为主机终态与设备结果：write_completed可以是主机终态，device_result=UNKNOWN 不得伪造 applied 来满足“都有终态”。

无需为了识别微调先实现 CFG/F/S/A。目前无需寻找另一个带击发功能的版本；若用户确认实际烧录的是其他版本，再以对应源码重做能力核对。本次未连接串口、修改旧源码或刷固件。
