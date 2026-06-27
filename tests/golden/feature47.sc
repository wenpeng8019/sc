# 由 scc --emit-sc 从 AST 再生成

tok level: "sensor.level"
    var b: i8 = (this->base: i8)
    var i: i8 = (this->input: i8)
    var m: i8 = b
    if i > b
        m = i
    return (m: @)

tok alert: "sensor.alert"

dep any: l:"sensor.level"
    var v: i8 = (l->get(): i8)
    if v > 100
        alert->set((1: @), 0)
    return false

fnc main: i4
    form level, (0: @)
    form alert, (0: @)
    level->set((50: @), 0)
    var lv: i8 = (level->get(): i8)
    var al: i8 = (alert->get(): i8)
    printf("after 50:  level=%lld alert=%lld\n", lv, al)
    level->set((150: @), 0)
    lv = (level->get(): i8)
    al = (alert->get(): i8)
    printf("after 150: level=%lld alert=%lld\n", lv, al)
    level->set((30: @), 0)
    lv = (level->get(): i8)
    printf("after 30:  level=%lld\n", lv)
    return 0
