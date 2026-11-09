# HA06 人主导微调实验

产品行为：YOLO 或仿真检测认出已选可靠目标，并且目标进入画面中心附近后，程序按实验限幅把该目标往中心送。人的大转向和显式接管优先。目标在区外时不追逐。

区域、死区、步长、速度、加速度、人工转向阈值和恢复等待都是实验值。改一处用场景文件顶部的 `assist_defaults`；单个场景写同名键可以覆盖。摘要里的 `parameters_experimental` 保持 true。

## 怎么跑

Windows 预设仍是 `windows-cpu` 的 `sim_runner.exe`。本批在 Linux 上用 g++ 和系统 OpenCV 4.6 复跑，没有跑 Visual Studio 预设，也没有 TensorRT。

```bash
cmake -S . -B out/linux-cpu -DCMAKE_CXX_COMPILER=g++ \
  -DVISION_WITH_OPENCV=ON -DVISION_WITH_TRT=OFF -DVISION_PROBE_ONLY=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build out/linux-cpu -j4 --target sim_test sim_runner
out/linux-cpu/sim_test configs/tracker.yml
out/linux-cpu/apps/sim_runner/sim_runner configs/sim-human-led-assist.yml \
  artifacts/human-led-assist/ha06-ha07-20261109/HA05 configs/tracker.yml
python3 tools/audit_human_led_assist.py artifacts/human-led-assist/ha06-ha07-20261109/HA05
python3 tools/run_ha06.py out/linux-cpu/apps/sim_runner/sim_runner configs/tracker.yml \
  artifacts/human-led-assist/ha06-ha07-20261109/HA06
```

`run_ha06.py` 会写出 `compare-on`、`compare-off`、`multi`、`faults`、`image`、`report.json` 和 `report.md`。输出目录必须是新的。`artifacts/` 不入库；本批数字快照在 `docs/evidence/ha06-ha07-20261109/`。

TensorRT 对照要在 Windows SDK 构建的 `vision_sim_image` 上追加最后一个参数。没有 Engine、程序和素材时，报告写 `not_run` / `UNKNOWN`，不把像素闭环说成 YOLO。

```text
vision_sim_image <engine> <sprite> <background> configs/tracker-body-primary.yml <新目录> \
  <target-x> <target-y> <target-w> <target-h> 1 1 0 0 0 0 human_led
```

同一条命令把最后一个词换成 `human_led_off` 就是关闭微调。`legacy` 保持旧的全画面追逐，不作为本产品的通过条件。

## 配置

| 文件 | 内容 |
|---|---|
| `configs/sim-human-led-compare.yml` | 同一人工轨迹。`assist_enabled: 1` 是开；工具复制一份改成 0。 |
| `configs/sim-human-led-multi.yml` | 第二个合成人物。分离，以及从旁走过并重叠。 |
| `configs/sim-human-led-faults.yml` | 0/80 ms 观察延迟、发送失败、反馈全丢、编码器全丢。 |
| `configs/sim-human-led-image.yml` | `image_detector: pixel`。渲染色块再检测，不是 TensorRT。 |

实验默认值：进入 48 px，退出 72 px，死区 1 px，单步 2 px，速度 50 px/s，加速度 500 px/s²，人工大转向 12 px，恢复等待 0.2 s。画面中心在 640×480 场景是 (320, 240)，像素闭环是 (160, 160)。

## 本批结果

协议 `ha06-v1`，进程退出码 0，`report.status=passed`。旧仿真的 SUCCESS/FAILED/UNKNOWN 原样保留，不用它替代中心误差。

| 场景 | 开 | 关 | 说明 |
|---|---:|---:|---|
| shared-static | 末误差 1 px，辅助 12.4 px | 末误差 12 px，辅助 0 | 人工先移动 8 px，两条轨迹的人工输入一致 |
| shared-drift | 末误差 2.00 px | 末误差 32.00 px | 10 px/s，执行器延迟 0，开着时留在中心附近 |
| shared-yield | 人工输入一致 | 辅助 0 | 30 px 主动转向，两边都收到同一次人的输入 |

像素闭环：开，末误差 1 px；关，末误差 20 px 且辅助为 0。检测器字段是 `pixel`。

双人：分离场景所选 ID 保持 1，末误差 1 px，辅助 20.4 px。交叉场景 ID 不切换，重叠时有歧义门控且该期间没有辅助命令，也没有对着远离真值的估计继续发步。两条的旧锁存结果都是 FAILED，因为 1 px 对 24 px 目标的归一化误差仍大于旧进入阈值 0.02。这不是人主导中心分数。

延迟和故障：

| 场景 | 旧结果 | 末误差 | 记录 |
|---|---|---:|---|
| delay-0 | FAILED / timeout | 1 px | 与 delay-80 的人工输入相同 |
| delay-80 | FAILED / timeout | 1 px | 60 帧内仍收到 1 px。只说明这个实验步长盖过了这段延迟，不说明延迟没有代价 |
| dispatch-fail | UNKNOWN / control | 20 px | 没有 applied，相机不动 |
| feedback-drop | UNKNOWN / control | 20 px | 超时后新辅助为 0。超时前的少量步长还在 |
| encoder-drop | FAILED / detection | 20 px | 没有 generated 命令 |

TensorRT：`not_run`，结果 UNKNOWN。
