# SA04 模型探针

CPU ONNX：executed / executed_coco_graph_not_project_contract

TensorRT：UNKNOWN / blocked。没有把缺 GPU 记成通过。

Windows 命令（未执行）：

```
cmake --build --preset windows-sdk --target vision_sim_image
out\windows-sdk\apps\trt_infer\Release\vision_sim_image.exe artifacts\engines\yolo11n-v002-retrain-20261110-fp32\best-fp32.engine <sprite> <background> configs\tracker-body-primary.yml <NEW_DIR> 19 42.4 431.2 674.9 1 1 0 0 0 0 human_led
```
