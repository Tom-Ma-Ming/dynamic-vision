# 本地识别预览测试版 · 2026-11-10

这是使用项目现有 best.onnx 的图片/录像识别预览，不是已验收的鼠标辅助版，也不是新训练的模型。不会操作鼠标。默认处理现有验证集，输出带框图片到独立新目录。

在项目目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools/preview-local-model.ps1
# 自选录像：
powershell -ExecutionPolicy Bypass -File tools/preview-local-model.ps1 -Source "完整录像路径"
```

默认用 ONNX Runtime CPU；训练使用现有 CUDA PyTorch。类别 0=head、1=person。原模型不覆盖。重新训练应从 .pt 权重或训练配置开始，不能将 .onnx 当作训练检查点。最新标注目录须先确认，再冻结数据、检查标签和训练/验证分离，之后训练并导出新的 ONNX。
