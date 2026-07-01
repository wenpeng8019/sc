# img_jpg_codec —— JPEG 熵扫描流式编解码（sc 侧 ABI 声明 + C 胶水绑定）
#
# 定位：**JPEG 专属熵层，非通用 codec**（区别于 builtins/codec 的 Huffman/rANS/DEFLATE 等通用原子）。
#   原属 builtins/codec 簇 9，因其满身 JPEG 印记（0xFF 塞字节、DC/AC (run,size)、dezigzag、
#   ZRL/EOB、RSTn 重启、progressive 谱选择+逐次逼近）而迁出，随 img_jpg 就近维护。
#
# 组织：由 img_jpg.sc 经 `add img_jpg_codec.sc` 内联为子模块；本文件再经 `add img_jpg_codec.c`
#   把 native 实现现场编译链接（路径相对本 .sc 目录解析）。契约唯一事实源即同目录 img_jpg_codec.c。
#
# 职责边界：只做位 I/O + Huffman + 逐块熵解码/编码 + 重启标记；MCU 几何 / IDCT / FDCT / 量化 /
#   上采样 / 颜色变换全归 img_jpg。块级即最小粒度，无独立非流式接口（整段解码 = 一次喂满、
#   need_more 永不触发；真正的整扫描一把梭需 MCU 几何，属 img 职责，故不提供）。

# 把 native 实现并入工程（路径相对本 .sc 目录解析）。
add img_jpg_codec.c

# ─────────────────── 解码：codec_jdec（逐块 checkpoint/回滚流式）───────────────────
# 解码逐块「块级 checkpoint / 回滚」流式（喂熵字节→产自然序原始未去量化系数 int16[64]；
# 字节不足即回滚等再喂）；编码同 zenc 的 feed/drain。
#
# 解码用法：codec_jdec_size() 分配 → init(s) → 每张 DHT 段 codec_jdec_dht(s,tc,th,counts,values)
#   建表 → codec_jdec_feed(s,in,inlen) 喂熵字节 → 循环 codec_jdec_block(...)==0 时补喂再重解；
#   RSTn 时 codec_jdec_reset(s)；scan 末经 codec_jdec_marker(s) 取终止 marker、
#   codec_jdec_pending(s,out,cap) 取回 marker 后残留字节续解容器；用完 codec_jdec_free(s)。
# 返回解码器状态结构字节数（供分配）。
@fnc codec_jdec_size:: u8
# 初始化解码器（清零 + marker=none）；返回 0。
@fnc codec_jdec_init:: i4, sp: &
# 交 Huffman 表：tc 0=DC/1=AC；th 0..3；counts[16] 各码长符号数；values 符号表。返回 0/-1。
@fnc codec_jdec_dht:: i4, sp: &, tc: i4, th: i4, counts: u1&, values: u1&
# 喂 inlen 字节熵流（append 进内部缓冲，先压实已消费前缀）；返回吸入字节数。
@fnc codec_jdec_feed:: i8, sp: &, in: &, inlen: u8
# baseline 解一块到 data[64]（自然序、未去量化）。返回 1 完成 / 0 缺字节(已回滚) / -1 错。
#   dc_pred 为该分量 DC 预测值（读改写；回滚时还原）。
@fnc codec_jdec_block:: i4, sp: &, data: i2&, dc_tbl: i4, ac_tbl: i4, dc_pred: i4&
# progressive DC 块（ah=succ_high, al=succ_low）。返回 1/0/-1。
@fnc codec_jdec_block_prog_dc:: i4, sp: &, data: i2&, dc_tbl: i4, dc_pred: i4&, ah: i4, al: i4
# progressive AC 块（ss=spec_start, se=spec_end, ah, al；内部维护 eob_run）。返回 1/0/-1。
@fnc codec_jdec_block_prog_ac:: i4, sp: &, data: i2&, ac_tbl: i4, ss: i4, se: i4, ah: i4, al: i4
# 重启标记复位（RSTn）：清位缓冲 + eob_run + marker。dc_pred 由 img 清。
@fnc codec_jdec_reset:: sp: &
# 返回 cj_grow 遇到的 marker XX（0xff=无）。
@fnc codec_jdec_marker:: i4, sp: &
# 取回 marker 之后未消费的内部缓冲字节到 out[0..cap)，返回拷贝字节数并推进。
@fnc codec_jdec_pending:: i8, sp: &, out: u1&, cap: u8
# 未消费字节数（供 img 分配 pending 缓冲）。
@fnc codec_jdec_pending_len:: u8, sp: &
# 释放内部输入缓冲（用完须调；随后 recycle 状态结构本身）。
@fnc codec_jdec_free:: sp: &

# ─────────────────── 编码：codec_jenc（逐块熵编码 + drain，同 zenc 模型）───────────────────
# 编码用法：codec_jenc_size() 分配 → init(s) → 每张标准 DHT codec_jenc_dht(s,tc,th,counts,values)
#   建表 → 每块 codec_jenc_block(s,du,dc_tbl,ac_tbl,&dc_pred)（du 为 img FDCT+量化+zigzag 出的
#   int[64]，du[0]=DC）→ codec_jenc_drain(s,out,cap) 抽扫描字节写 com；末 codec_jenc_flush(s) 位
#   对齐后再 drain（EOI 由 img 写）；用完 codec_jenc_free(s)。
# 返回编码器状态结构字节数（供分配）。
@fnc codec_jenc_size:: u8
# 初始化编码器（清零）；返回 0。
@fnc codec_jenc_init:: i4, sp: &
# 交标准编码 Huffman 表：tc 0=DC/1=AC；counts[16]/values（img 亦据此写 DHT 段）。返回 0/-1。
@fnc codec_jenc_dht:: i4, sp: &, tc: i4, th: i4, counts: u1&, values: u1&
# 熵编码一块（du[64] zigzag 序、量化后、du[0]=DC；dc_pred 读改写）。返回 1 完成 / -1 内存错。
@fnc codec_jenc_block:: i4, sp: &, du: i4&, dc_tbl: i4, ac_tbl: i4, dc_pred: i4&
# 抽产出到 out[0..cap)，返回抽出字节数（out 满即抽走后续编码）。
@fnc codec_jenc_drain:: i8, sp: &, out: u1&, cap: u8
# 收尾位对齐（末尾 fillBits）。返回 0/-1。之后再 drain 抽尾字节。
@fnc codec_jenc_flush:: i4, sp: &
# 释放内部输出缓冲（用完须调；随后 recycle 状态结构本身）。
@fnc codec_jenc_free:: sp: &
