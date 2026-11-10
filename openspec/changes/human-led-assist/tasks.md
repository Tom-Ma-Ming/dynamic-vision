# 实施任务

项目预设仍是 `cmake --build --preset windows-cpu` 与 `ctest --preset windows-cpu --output-on-failure`。本批没有 Visual Studio 预设，实际复跑是 Linux g++：`cmake -S . -B out/linux-cpu -DCMAKE_CXX_COMPILER=g++ -DVISION_WITH_OPENCV=ON -DVISION_WITH_TRT=OFF -DVISION_PROBE_ONLY=OFF -DCMAKE_BUILD_TYPE=Release`，然后 `sim_test configs/tracker.yml`。输出目录 `artifacts/human-led-assist/ha06-ha07-20261109/`。入库快照在 `docs/evidence/ha06-ha07-20261109/`。Windows 预设本批未重跑。

- [x] HA01：Simulator.hpp / sim_runner 定义显式模式、区域、单位和输入输出字段；配置加载与无效参数测试通过。`sim_test` 拒绝退出小于进入、死区越界、缺中心和倒退的人工时间。
- [x] HA02：simulator.cpp 接入独立人工镜头轨迹及让步状态机，tests/unit/test_sim.cpp 验证接管、释放、退出与积累清除。
- [x] HA03：实现二维限幅及可靠性门控，补区内上下/斜向、区外无输出和边界抖动测试；记录实际参数。数值仍是实验值。
- [x] HA04：反馈因果、当前协方差、坐标时钟和有界调度由既有 `sim_test` 反例覆盖；旧大偏移仍走 legacy，不记入人主导成功率。
- [x] HA05：`configs/sim-human-led-assist.yml` 与 `tools/audit_human_led_assist.py` 已跑。审计 `human-led-audit-v1` 为 passed，需求帧覆盖率 1.0。旧 SUCCESS 锁存不是该协议。
- [x] HA06：同源人工轨迹开/关、像素图像闭环、双人和延迟故障已跑，报告保留 FAILED/UNKNOWN。YOLO ONNX 闭环已执行：开 1 px、关 24 px，120 帧有人物框。TensorRT 因本机无 NVIDIA GPU 不能执行，状态 blocked/UNKNOWN，Windows 命令仍是 `vision_sim_image ... human_led`。见 docs/ha06-experiments.md。
- [x] HA07：代码、配置、测试和证据清单在 docs/ha07-batch-review.md。路线图已更新。实验参数单独列出。本文件是交给独立复审的材料，不是复审已经完成。
