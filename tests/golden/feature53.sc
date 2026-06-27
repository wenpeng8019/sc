# 由 scc --emit-sc 从 AST 再生成

tok root: "rch.root"

tok m1: "rch.m1"

tok m2: "rch.m2"

tok leaf1: "rch.leaf1"

tok leaf2: "rch.leaf2"

dep all: s:"rch.root" map t:"rch.m1"
    return false

dep all: s:"rch.root" map t:"rch.m2"
    return false

dep all: s:"rch.m1" map t:"rch.leaf1"
    return false

dep all: s:"rch.m2" map t:"rch.leaf2"
    return false

fnc main: i4
    printf("node     reach\n")
    printf("root     %5d\n", root->reach())
    printf("m1       %5d\n", m1->reach())
    printf("m2       %5d\n", m2->reach())
    printf("leaf1    %5d\n", leaf1->reach())
    printf("leaf2    %5d\n", leaf2->reach())
    return 0
