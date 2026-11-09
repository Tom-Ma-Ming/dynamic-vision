# 人主导镜头与有限辅助

用户于 2026-11-09 明确：人工负责大幅转向，程序仅在中心附近微调。旧仿真把大偏差自主追逐作为优化重心，不能代表该产品需求。

范围：自建仿真的人工输入、辅助区域、选择可靠性、让步状态、限幅、可复算评分；保留检测和 MOT。受影响入口为 Simulator.hpp、simulator.cpp、sim_runner、vision_sim_image 的可选末参、配置、单元测试及审计工具。本批 Linux 运行见 docs/ha07-batch-review.md。Windows 预设和 TensorRT 实跑没有在本批完成。

不新增控制策略训练、自动全画面选人或实际设备控制。检测优化及身份数据后续单独推进。详细契约与验收依据 docs/human-led-assist-handoff.md。
