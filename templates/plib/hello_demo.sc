# hello_demo —— plib 跨平台库模板演示
#
# 用法：cd templates/plib && ./build.sh && scc hello_demo.sc

inc io.sc
inc plib.sc

fnc main: i4
    var msg: const char& = plib_hello()
    print "plib says: ", msg

    var sum: i4 = plib_add(3, 4)
    print "plib_add(3, 4) = ", sum

    return 0
