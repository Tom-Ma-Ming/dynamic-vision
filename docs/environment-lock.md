# Windows x64 环境锁定 · 2026-11-06

| 项目 | 本机实测 |
|---|---|
| GPU | GeForce GTX 1060 6GB，SM 6.1，nvidia-smi 显存 6144 MiB |
| 驱动 | 582.78，cudaDriverGetVersion 返回 13000 |
| CUDA | Toolkit 11.8.89，runtime 11080 |
| TensorRT | F:\cf-build\TensorRT-8.6.1.6，头文件与运行库均返回 8601 |
| nvinfer.dll SHA-256 | 96A5E25EF7BCF3F885863372B1EB66C879BAD845CACDD35064C7D48FA62638CA |
| 工具链 | VS 2019，MSVC 19.29.30159，Windows SDK 10.0.19041.0，CMake 3.20.21032501 |
| 源码参照 | TensorRT-11.4/VERSION 为 11.4.0.106；11.4 GA 运行组合待验证 |
| 模型阶段 | YOLO11n 320 FP32，ONNX opset 17，best.onnx SHA-256 79D15166…；见 artifacts/runs/yolo11n-v002-320/weights/onnx-contract.json |

在 dynamic-vision 目录执行以下 PowerShell。新 SDK 目录应使用新的构建目录，避免复用 CMake 查找缓存。

~~~powershell
$env:Path = 'F:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;' + $env:Path
$env:TensorRT_ROOT = 'F:\cf-build\TensorRT-8.6.1.6'
cmake --preset windows-sdk
cmake --build --preset windows-sdk
ctest --preset windows-sdk -V
~~~

无 SDK 配置将上述三条命令的预设改为 windows-base。两组 CTest 原始输出分别保存于本目录 windows-sdk.log、windows-base.log。
