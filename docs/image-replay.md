# 本机图片检测与视频回放

在应用项目目录的 PowerShell 中运行。工具链为 VS 2019、CUDA Toolkit 11.8、TensorRT 8.6.1.6 和 OpenCV 4.13.0。模型固定输入 1×3×320×320 RGB FP32，输出 1×6×2100。类别为 head、person。当前对照阈值为置信度 0.25、逐类 NMS IoU 0.45。

~~~powershell
$env:TensorRT_ROOT = 'F:\cf-build\TensorRT-8.6.1.6'
$env:Path = "F:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;F:\opencv\opencv\build\x64\vc16\bin;$env:TensorRT_ROOT\lib;$env:CUDA_PATH\bin;$env:Path"
cmake --preset windows-sdk -DVISION_WITH_OPENCV=ON -DOpenCV_DIR=F:/opencv/opencv/build
cmake --build --preset windows-sdk
ctest --preset windows-sdk
$bin = 'out/windows-sdk/apps/trt_infer/Release'
$engine = 'artifacts/engines/yolo11n-v002-fp32/best-fp32.engine'
$image = (Get-ChildItem artifacts/datasets/yolo_v002/images/val/*.jpg | Select-Object -First 1).FullName
& "$bin/vision_detect.exe" $engine $image
& "$bin/vision_replay.exe" $engine artifacts/replay/v1/fixture.avi artifacts/replay/my-run
~~~

回放命令需要一个新的输出目录。frames.jsonl 包含从 0 开始的帧号、媒体时间戳（纳秒）、时间来源、原始图像像素坐标 xyxy、类别和分数。媒体时间缺失或倒退时由 FPS 估算并显式标记。overlay.mp4 使用源标称 FPS 编码，变帧率输入请以 JSONL 时间为准。summary.json 给出包含预处理、推理和解码的耗时分位数，不含视频读写。

回归入口为 tools/verify_image.py、tools/verify_trt.py 和 tools/verify_replay.py，均支持 --help。回放测试将 200 张采集图片按文件名排序，以 25 FPS 循环拼成 1000 帧夹具；它用于验证顺序与可重复性，不能用来估计真实人物运动速度。
