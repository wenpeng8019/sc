# 由 scc --emit-sc 从 AST 再生成

@@

inc feature30_mod.sc

@def metric: {
    tag: char&
    value: i4
}

@fnc app_report: m: metric
    printf("[report] %s = %d\n", m.tag, m.value)

fnc main: i4
    sensor_sample("temp", 21)
    sensor_sample("humidity", 55)
    return 0
