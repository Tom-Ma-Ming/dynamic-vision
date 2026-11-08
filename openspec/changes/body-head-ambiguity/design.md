# 设计
separate_head_ambiguity 默认关闭。启用时单一人物框中多个头候选清除头测量并标记 headAmbiguous，保留人物身份状态；跨多个人物的头归属冲突仍保留身份歧义。冲突人物不能用旧头偏移匹配仅头观测；仅头轨迹也不能用冲突头观测切换人物锚点。普通关联及 ByteTrack 共用规则。

头部测量契约：`lastHeadSeen` 为最近一次具有头测量、且分组和最终轨迹关联均无歧义的采集时间。普通关联、ByteTrack高低分两阶段和新生轨迹统一记录。缺测、冲突、Lost或预测不刷新时间；从未获取可靠头测量时为null。`headMeasuredAt(snapshot.timestamp)` 仅在该时刻有可靠测量且当前不是Lost/歧义时为true，不能由旧headOffset或person比例推导true。该字段表示算法接受的测量，不证明检测正确，也不替代Confirmed状态或下游身份门控。

JSON输出 `head_measured` 和 `last_head_observed_ts_ns`，与帧的采集时钟同域；视频head+表示本帧接受了测量、head?表示头部候选冲突、head-表示无本帧可靠测量。历史时间保留便于计算年龄，不输出伪造的头部实测坐标。未来预测快照不会携带本帧测量标志。
