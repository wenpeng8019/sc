# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

inc m.sc

inc async.sc

rpc greet: char&, name: char&, ms: u4
    printf("  [%s] 睡 %u ms...\n", name, ms)
    await delay(ms)
    printf("  [%s] 醒来\n", name)
    return name

rpc both: i4, a: char&, b: char&
    var x: char& = await greet(a, 60)
    var y: char& = await greet(b, 30)
    printf("  both 收集: %s + %s\n", x, y)
    return 0

rpc square_worker: f: future&, n: i4
    done f, n * n

fnc bg_square: future&, n: i4
    var f: future& = future()
    run square_worker(f, n)
    return f

rpc compute: i4, n: i4
    var a: i4 = await bg_square(n)
    var b: i4 = await bg_square(a)
    printf("  compute: %d -> %d\n", n, b)
    return b

fnc main: i4
    async_init()
    var fa: future& = async greet("A", 80)
    var fb: future& = async greet("B", 30)
    async_loop()
    printf("fa = %s\n", (fa->get(): char&))
    printf("fb = %s\n", (fb->get(): char&))
    var fc: future& = async both("X", "Y")
    async_loop()
    printf("both ret = %d\n", (fc->get(): i4))
    var fd: future& = async compute(3)
    async_loop()
    printf("compute ret = %d\n", (fd->get(): i4))
    async_final()
    return 0
