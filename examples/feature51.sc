# feature51：token 依赖图关键路径 + 松弛（dep…map）—— 流水线瓶颈识别（无权最长链）。
#
# 二梯队机制：在 dep…map 显式 DAG 之上，编译期对最长链做关键路径分析，烘焙为每个 token 的
#   「critical（是否在最长链上）+ slack（松弛余量）」字面量常量（O(1) 查表，运行时零图遍历）。
#   · 正向最早深度 e[v]=depth；反向最长深度 rd[v]（v 到任一汇点的最长距离）；
#   · 关键路径总长 L=max(e[v]+rd[v])；critical ⟺ e[v]+rd[v]==L；slack=L-(e[v]+rd[v])。
#   · critical 节点：加长它即拖慢整条流水线（瓶颈）；slack>0 节点：有余量可慢 slack 跳。
#
# 本例搭一条媒体流水线 DAG：
#   fetch → decode → render → output     主链（视频，4 级，关键路径）
#   fetch → audio  → output              旁支（音频，少一级 → slack=1）
# 汇点 output 取两前驱最早深度的 max=3；音频支 audio 的 e+rd=1+1=2 < L=3 → slack=1。

tok fetch:  "pipe.fetch"     # 源（depth 0）
tok decode: "pipe.decode"    # 视频解码（depth 1）
tok render: "pipe.render"    # 视频渲染（depth 2）
tok audio:  "pipe.audio"     # 音频解码（depth 1；旁支）
tok output: "pipe.output"    # 汇点输出（depth 3）

dep all: s:"pipe.fetch"  map t:"pipe.decode"
    return false
dep all: s:"pipe.decode" map t:"pipe.render"
    return false
dep all: s:"pipe.render" map t:"pipe.output"
    return false
dep all: s:"pipe.fetch"  map t:"pipe.audio"
    return false
dep all: s:"pipe.audio"  map t:"pipe.output"
    return false

fnc main: i4
    ::printf("stage     depth crit slack\n")
    ::printf("fetch     %5d %4d %5d\n", fetch->depth(),  fetch->critical(),  fetch->slack())
    ::printf("decode    %5d %4d %5d\n", decode->depth(), decode->critical(), decode->slack())
    ::printf("render    %5d %4d %5d\n", render->depth(), render->critical(), render->slack())
    ::printf("audio     %5d %4d %5d\n", audio->depth(),  audio->critical(),  audio->slack())
    ::printf("output    %5d %4d %5d\n", output->depth(), output->critical(), output->slack())
    return 0
