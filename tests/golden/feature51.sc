# 由 scc --emit-sc 从 AST 再生成

tok fetch: "pipe.fetch"

tok decode: "pipe.decode"

tok render: "pipe.render"

tok audio: "pipe.audio"

tok output: "pipe.output"

dep all: s:"pipe.fetch" map t:"pipe.decode"
    return false

dep all: s:"pipe.decode" map t:"pipe.render"
    return false

dep all: s:"pipe.render" map t:"pipe.output"
    return false

dep all: s:"pipe.fetch" map t:"pipe.audio"
    return false

dep all: s:"pipe.audio" map t:"pipe.output"
    return false

fnc main: i4
    ::printf("stage     depth crit slack\n")
    ::printf("fetch     %5d %4d %5d\n", fetch->depth(), fetch->critical(), fetch->slack())
    ::printf("decode    %5d %4d %5d\n", decode->depth(), decode->critical(), decode->slack())
    ::printf("render    %5d %4d %5d\n", render->depth(), render->critical(), render->slack())
    ::printf("audio     %5d %4d %5d\n", audio->depth(), audio->critical(), audio->slack())
    ::printf("output    %5d %4d %5d\n", output->depth(), output->critical(), output->slack())
    return 0
