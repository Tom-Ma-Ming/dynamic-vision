# 设计

使用用户确认的原 review 目录，先只读审计；结构错误则拒绝评估。复制图像和 JSON 后对照源审计哈希，防止评估期间混入修改。映射中的源帧号必须连续，PNG 与原录像对应像素相同，预测时间戳 ns 与 manifest 一致。以每段自身的 group_id 空间评分。

CV/IMM 读取同一来源的完整回放结果，不重新初始化片段内跟踪；逐帧检测相同才继续。检测使用固定 conf 0.25、IoU 0.5、最大匹配数量优先的一对一匹配。所有 Confirmed（含 ambiguous）统一轨迹框对 person GT 评分；缺少轨迹来源类别字段的限制明确记录。

输出冻结 labels、aligned.jsonl、输入哈希、库版本、分数和必填质量说明。输出目录必须新建；异常保留中间产物、不覆盖源文件。状态固定为 provisional_diagnostic_not_acceptance。失败不调整阈值来改善结果。
