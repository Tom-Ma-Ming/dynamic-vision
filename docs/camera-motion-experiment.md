# 背景平移补偿实验 · 2026-11-08

已实现默认关闭的背景平移补偿，配置 configs/tracker-camera.yml。模型、训练数据和默认 tracker.yml 不变。它用于离线录像中的轨迹关联实验，不提供真实输入控制，也未预测未来镜头运动。

## 实现与坐标含义

CameraMotionEstimator 在回放推理线程中缓存上一已处理帧的灰度图、前景掩码和采集时间戳。先屏蔽检测框（外扩 8 px）、5% 边缘及中心半径 12 px 区域，使用加窗相位相关提供平移初值，再以 Shi-Tomasi 特征和两层 LK 前后向光流核实。初值不是最终补偿依据：最终仍需通过数量、残差共识及空间覆盖检查。

实验配置要求至少 20 个内点、内点占原背景特征数至少 65%、覆盖至少 15% 图像面积及 3×3 网格的 4 格；前后向误差不超过 1.5 px、平移残差不超过 2 px、单次位移不超过短边 25%、间隔不超过 200 ms。初值最低相位响应 0.1，特征质量阈值 0.01、最小间距 8 px、LK 窗口 21×21 是本版固定算法常量，不属于已标定通用最优值。

accepted 位移表示“上一已处理图像中的背景点，到当前图像的移动”，直接加到已经预测到当前采集时间的轨迹位置；不是相机物理移动量。方差下限 0.25 px²，依据内点残差作启发式估计，未经过实景不确定性标定。所有 IMM 分模型同步平移并增加位置方差，再重新融合；不改变 ID、尺寸、lastSeen 和头身相对偏移。只发生镜头位移而没有观测时，轨迹仍为 Lost。

位置始终属于当前图像坐标。启用后速度倾向于扣除已成功补偿的背景运动；补偿拒绝时回退普通观测更新，可能再次混入相机运动。该速度不是世界速度。predictAt 不模拟未来镜头轨迹；旋转、缩放、强视差、未知前景和 HUD 仍可能使本平移模型失效。

开启时 JSONL 增加 camera_motion（accepted、reason、源/目标时间、位移、方差、特征/内点/覆盖信息）和 camera_ms；summary 增加 camera_ms 分位数。mot_ms 不含背景估计；result_age 包含它，仍不含视频解码及叠加写入。拒绝估计的位移/方差输出为 0。关闭时不增加这些字段，原行为保持一致。

## 真实录像诊断

完整回放 rec_20261107_202107、rec_20261107_202949 各 5400 帧。与既有 CV 输入检测逐帧相同，10800 帧均完整输出并解码，失败/丢帧/乱序均为 0。评分使用上一阶段冻结的 180 帧 PNG/JSON，没有根据新算法修改真值或阈值；质量状态继续为 provisional，详见 docs/reviewed-clip-evaluation.md。

| 片段 | 配置 | HOTA % | IDF1 % | IDSW |
|---|---|---:|---:|---:|
| clip01 | CV | 44.85 | 61.39 | 5 |
| clip01 | CV + 背景平移 | 44.42 | 61.76 | 4 |
| clip02 | CV | 23.77 | 25.00 | 24 |
| clip02 | CV + 背景平移 | 30.88 | 32.47 | 15 |

第二段有改善，第一段 HOTA 略降；不能据此切换默认配置。两组检测相同，因此 person/head 检测分数与上一阶段一致，补偿没有提高 YOLO 召回。

| 原录像 | 通过内部质量门控 | camera_ms P50 / P95 / P99 |
|---|---:|---:|
| 202107 | 1464/5400（27.11%） | 5.960 / 9.524 / 13.063 |
| 202949 | 2099/5400（38.87%） | 5.105 / 8.517 / 12.305 |

大多数拒绝原因是 low_consensus、前后向有效点不足或弱纹理。评分窗口内的通过率分别为 76/90、53/90；与整段视频不同，不能以选中窗口代替全片可靠性。门控通过率不是估计准确率，目前没有真实镜头位移真值。

以上计时为单次回放测量，受机器调度影响，不是重复性能基准。第一段估计耗时 max 为 567.956 ms，第二段 50.220 ms；没有据此宣称实时延迟已达标。

## 测试和兼容性

- CPU CTest 26/26、SDK CTest 6/6、相关 Python 工具 20/20。
- 已知平移误差 <0.5 px；独立移动前景被屏蔽；无纹理、局部纹理、场景突变、长间隔、分辨率变化、过期时间戳有拒绝/重置测试。
- CV/IMM × 普通/ByteTrack 测试覆盖静止人物随背景平移时保持 ID、速度不吸收该平移、Lost 不伪装为观测、头身锚点转换、模式融合一致性、预测无副作用、错时/非有限补偿拒绝且状态不变。
- 关闭补偿：两轮 1000 帧稳定字段完全一致，并逐字段等于旧 imm-disabled-v1 基线。
- 开启补偿：两轮 1000 帧稳定字段（包括 camera_motion）重复一致；camera_ms 等耗时不参与语义哈希。此截图循环夹具只证明工程重复性，不证明运动准确率。

## 复现与证据

按 docs/image-replay.md 设置 PATH 后构建；OpenCV C++ 依赖新增 video 模块。

```powershell
cmake --build --preset windows-cpu
ctest --preset windows-cpu --output-on-failure
cmake --build --preset windows-sdk
ctest --preset windows-sdk --output-on-failure
New-Item -ItemType Directory -Path artifacts/camera-repro/replay -Force
out/windows-sdk/apps/trt_infer/Release/vision_replay.exe artifacts/engines/yolo11n-v002-fp32/best-fp32.engine docs/rec_20261107_202107.avi artifacts/camera-repro/replay/rec_20261107_202107-camera configs/tracker-camera.yml
# 第二段按同样命令回放 rec_20261107_202949。
.\.venv\Scripts\python tools/verify_camera_replays.py artifacts/real-recordings-v1/replay artifacts/camera-repro/replay artifacts/camera-repro/verification.json
```

评分工具现支持 --methods cv camera；其 replay 根目录需存在对应 *-cv、*-camera 的 frames.jsonl。原来的默认 cv/imm 调用兼容。本次命令及结果使用 artifacts/camera-motion-v1：replay、verification.json、scores/report.json、scores/labels、disabled-replay、enabled-repeat、构建与测试日志。代码和报告入库，artifacts 沿用本机证据管理约定。

结论：平移补偿第一版可运行、可拒绝不可信估计、可回退，已有有限诊断收益，但覆盖率、复杂镜头运动和延迟尾部仍需改善；保持默认关闭。后续先分析拒绝/失配场景，另行完善仿真动态延迟与控制标定，ReID 和训练精度优化继续按任务计划推进。
