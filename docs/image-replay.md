# 本机图片检测与视频回放

在应用项目目录的 PowerShell 中运行。工具链为 VS 2019、CUDA Toolkit 11.8、TensorRT 8.6.1.6 和 OpenCV 4.13.0。模型固定输入 1×3×320×320 RGB FP32，输出 1×6×2100。类别为 head、person。当前对照阈值为置信度 0.25、逐类 NMS IoU 0.45。

~~~powershell
$env:TensorRT_ROOT = 'F:\cf-build\TensorRT-8.6.1.6'
$env:Path = "F:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;F:\opencv\opencv\build\x64\vc16\bin;$env:TensorRT_ROOT\lib;$env:CUDA_PATH\bin;$env:Path"
cmake --preset windows-sdk -DVISION_WITH_OPENCV=ON -DOpenCV_DIR=F:/opencv/opencv/build
cmake --build --preset windows-sdk
ctest --preset windows-sdk
# 无 TRT 的 CPU 全量测试（vision_core + GTest）
cmake --preset windows-cpu -DOpenCV_DIR=F:/opencv/opencv/build
cmake --build --preset windows-cpu
ctest --preset windows-cpu
$bin = 'out/windows-sdk/apps/trt_infer/Release'
$engine = 'artifacts/engines/yolo11n-v002-fp32/best-fp32.engine'
$image = (Get-ChildItem artifacts/datasets/yolo_v002/images/val/*.jpg | Select-Object -First 1).FullName
& "$bin/vision_detect.exe" $engine $image
# 默认 blocking：严格逐帧、可复现
& "$bin/vision_replay.exe" $engine artifacts/replay/v1/fixture.avi artifacts/replay/my-run
# 显式 realtime：latest 队列、按媒体时间节奏，慢消费者时丢旧待处理帧
& "$bin/vision_replay.exe" $engine artifacts/replay/mot-v1/fixture.avi artifacts/replay/my-realtime --config "$bin/replay-realtime.yml"
~~~

回放命令需要一个新的输出目录。`vision_replay` 默认读取可执行文件旁的 tracker.yml 和 replay.yml；`--config` 可以指定其他回放配置。配置项为 queue_capacity（1 或 2）、queue_mode（blocking 或 latest）、realtime_pace（0 或 1），未知键或越界值启动即报错；realtime_pace=1 时必须使用 latest。blocking 模式不丢帧，用于离线评估；latest 模式在队列满时只替换尚未消费的旧帧，正在推理的帧不会被修改。tracker 第 4 个位置参数可传 configs/tracker-bytetrack.yml 打开低置信度二次关联（默认关闭，见 docs/mot-ablation.md）。

frames.jsonl 包含从 0 开始的帧号、媒体时间戳（纳秒）、时间来源、原始图像像素坐标 xyxy、类别和分数。媒体时间缺失或倒退时由 FPS 估算并显式标记。新增字段：capture_steady_ns（模拟采集时刻）、publish_steady_ns（快照发布时刻）、queue_wait_ms（入队到出队）、infer_ms（预处理+推理+解码+NMS）、mot_ms（跟踪更新）、result_age_ms（capture 到 publish，不含叠加写入）。overlay.mp4 使用源标称 FPS 编码，变帧率输入请以 JSONL 时间为准；realtime 模式丢帧时 overlay 与 JSONL 都只保留已处理帧，frame_id 会出现空洞。

summary.json 包含 frames、captured、processed、replaced、rejected、pending、stale、failed、capacity、mode、paced、peak，以及 queue_wait_ms、infer_ms、mot_ms、result_age_ms 四组 P50/P95/P99/max/n；旧字段 detect_p50_ms、detect_p95_ms、mot_p50_ms、mot_p95_ms 保留。blocking 模式下 captured = processed，realtime 模式下 captured = processed + replaced（正常结束）。计时从 capture.read 返回后开始，排除视频解码与叠加写入。

回归入口为 tools/verify_image.py、tools/verify_trt.py 和 tools/verify_replay.py，均支持 --help。回放测试将 200 张采集图片按文件名排序，以 25 FPS 循环拼成 1000 帧夹具；它用于验证顺序与可重复性，不能用来估计真实人物运动速度。

2026-11-08：可用 tracker 第 4 个位置参数 configs/tracker-camera.yml 显式启用背景平移补偿；默认关闭。新增 camera_motion 质量/位移记录及 camera_ms，mot_ms 不含它，result_age 包含它。模型仅补偿已观测的背景平移，不处理未来镜头运动或完整旋转/缩放。OpenCV 构建增加 video 模块，实验与限制见 docs/camera-motion-experiment.md。
