# 由 scc --emit-sc 从 AST 再生成

def make_counter: nm
    tls cnt_\nm: i4 = 0
    fnc bump_\nm: i4
        cnt_\nm = (cnt_\nm + 1)
        return cnt_\nm

mix make_counter(a)

mix make_counter(b)

fnc main: i4
    ::printf("%d %d %d\n", bump_a(), bump_a(), bump_b())
    return 0
