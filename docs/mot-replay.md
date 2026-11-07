# 多目标离线回放

构建沿用 image-replay.md 中的 windows-sdk 与 OpenCV 选项。vision_replay 默认读取可执行文件旁的 tracker.yml 和 replay.yml；tracker 配置可作为第 4 个位置参数，回放队列配置用 --config 指定，详见 image-replay.md。配置中的观测噪声、运动噪声、门控、最小确认次数和失效时间属于工程初值。

在项目根目录打开交互复核工具：

~~~powershell
.\.venv\Scripts\python.exe tools/review_tracks.py artifacts/replay/mot-v1/fixture.avi artifacts/replay/mot-v1/first/frames.jsonl
~~~

空格播放/暂停，A/D 或左右方向键逐帧，左键点击无身份歧义的 Confirmed 框选择 ID，右键清除，Esc 退出。暂失保留 ID，删除或歧义时显示 Unavailable。播放器消费已经生成的轨迹。命令行也可传入选择文件（最后两个参数依次为配置和选择文件），每行写“帧号 轨迹ID”，ID 0 表示清除。示例在 artifacts/replay/mot-v1/selection-demo.txt。

每帧的 tracks 使用原始图像像素，速度为像素/秒，协方差是 [cx,cy,vx,vy] 行优先 4×4 矩阵，mahalanobis_squared 是平方创新距离。Lost 的关联代价与创新值为 -1。removed_ids 用于标记本帧删除的轨迹。媒体时间轴单位为纳秒。回放时间戳属于合成视频时钟。

复现实验：CTest 的 mot_synthetic 检查 1/2/5/10 人、交叉、短时遮挡、仅头部观测、失效、选择和拒绝倒序。可用下列命令重算双人真值测试指标。评估仅使用 Confirmed 输出，真值包含短时遮挡人物；评估输入每帧含 gt、tracks 两组 [id,x1,y1,x2,y2]。
~~~powershell
out/windows-sdk/Release/mot_test.exe configs/tracker.yml artifacts/replay/mot-v1/synthetic.jsonl
.\.venv\Scripts\python.exe tools/score_mot.py artifacts/replay/mot-v1/synthetic.jsonl artifacts/replay/mot-v1/synthetic-metrics.json
~~~
本机使用 TrackEval 1.3.0 和 SciPy 1.18.1。暂无真实录像跨帧身份真值，不能在截图拼接夹具上报告实景身份准确率。
