# 实施任务（未验收）

统一验证入口：CPU 构建 `cmake --build --preset windows-cpu`，`ctest --preset windows-cpu --output-on-failure`。各任务须交回独立输出目录 artifacts/human-led-assist/<版本>/<任务ID>/，记录实际配置、摘要和运行命令。下述新配置/脚本尚待实现，不能将计划命令称为已执行。

- [ ] HA01：Simulator.hpp / sim_runner 定义显式模式、区域、单位和输入输出字段；配置加载与无效参数测试通过。
- [ ] HA02：simulator.cpp 接入独立人工镜头轨迹及让步状态机，tests/unit/test_sim.cpp 验证接管、释放、退出与积累清除。
- [ ] HA03：实现二维限幅及可靠性门控，补区内上下/斜向、区外无输出和边界抖动测试；记录实际参数。
- [ ] HA04：修复反馈因果、当前协方差、坐标时钟和有界调度；复跑已有时钟/反馈检查，旧大偏移反例保留为旧模式诊断。
- [ ] HA05：新增 configs/sim-human-led-assist.yml 与 tools/audit_human_led_assist.py，先用 sim_runner 运行新目录，再独立审计；包含覆盖率、效果与人工接管残余位移，不能全程不辅助获胜。
- [ ] HA06：同源人工轨迹下完成关闭/开启对照，以及 TensorRT 图像闭环、多人物与延迟故障验证；报告保留失败与未知。
- [ ] HA07：完整提交代码、配置、测试和证据清单；更新路线图并交 Codex 独立复审，产品参数未确认项单列。
