# 由 scc --emit-sc 从 AST 再生成

@@

@inc feature46_config.sc

@inc feature46_logger.sc

fnc main: i4
    config.set_level(3)
    logger.emit("hello")
    logger.emit("world")
    ::printf("config.level = %d, logger.count = %d\n", config.level(), logger.count())
    return 0
