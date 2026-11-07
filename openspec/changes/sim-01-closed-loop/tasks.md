# 执行任务

- [x] S01 实现 include/vision/sim/Simulator.hpp 与 src/sim/simulator.cpp 的二维世界、观测、执行器、闭环状态机。
- [x] S02 增加 configs/sim.yml 与 apps/sim_runner，输出 runs.jsonl/summary.json/report.md。
- [x] S03 增加 tests/unit/test_sim.cpp，覆盖 T12 的 SUCCESS/FAILED/UNKNOWN、遮挡恢复、确定性和无重试。
- [x] S04 接入 CMake 的 windows-cpu/windows-full 构建与 sim_closed_loop CTest；CPU 21/21。
- [x] S05 跑 CTest/pytest/Eval 报告，更新 docs 与交接状态；EVAL gates 7/3/2。
