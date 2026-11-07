# IMM 实验开关

目标：提高急停、变向时的跟踪与预测适应性，保留原 CV 基线。原 CV 在这些场景丢失选中轨迹，提高单模型过程噪声又加大小目标平稳误差。

范围：mot::Tracker 增加默认关闭的 stationary/CV/maneuver IMM；接入普通/ByteTrack 关联、predictAt、回放 JSONL 和模拟控制/评分；三种子与两种 CV 对照。

影响入口：src/tracking、apps/trt_infer/tracking_output.hpp、apps/sim_runner、configs/tracker-imm.yml；默认 tracker.yml 不变。

后续：真实身份评估、参数标定、镜头补偿和 ReID；本实验不承诺 person 召回 95%。
