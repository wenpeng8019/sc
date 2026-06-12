cmake_minimum_required(VERSION 3.16)
project(scc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(scc
    src/main.cpp
    src/lexer.cpp
    src/parser.cpp
    src/ast_print.cpp
    src/ast_json.cpp
    src/codegen_c.cpp
    src/codegen_sc.cpp
    src/semantic.cpp
)

target_compile_options(scc PRIVATE -Wall -Wextra)

# ------------------------------------------------------------
# 发行版变体：-DSCC_EMBED_BUILTINS=ON
#   builtins 的 .sc/.h 资源 + 预编译 adt.a 内嵌进 scc 二进制，
#   运行时释放到 ~/.cache/scc/builtins-<内容哈希>（见 main.cpp），
#   单二进制发行无需携带 builtins 目录
# ------------------------------------------------------------
option(SCC_EMBED_BUILTINS "内嵌 builtins 资源与预编译 adt 静态库（单二进制发行）" OFF)
if(SCC_EMBED_BUILTINS)
    get_filename_component(BUILTINS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../builtins ABSOLUTE)
    # 资源清单：所有 .sc/.h（configure 时收集，builtins 增删文件后需重新 cmake）
    file(GLOB_RECURSE BUILTIN_RES RELATIVE ${BUILTINS_DIR}
         ${BUILTINS_DIR}/*.sc ${BUILTINS_DIR}/*.h)

    # 预编译默认 adt 实现 → adt.a（内嵌后运行时直接参与链接，不再现场编译）
    set(ADT_A ${CMAKE_CURRENT_BINARY_DIR}/adt.a)
    add_custom_command(OUTPUT ${ADT_A}
        COMMAND cc -O2 -I ${BUILTINS_DIR}/adt -c ${BUILTINS_DIR}/adt/adt_impl.c
                -o ${CMAKE_CURRENT_BINARY_DIR}/adt_impl.o
        COMMAND ar rcs ${ADT_A} ${CMAKE_CURRENT_BINARY_DIR}/adt_impl.o
        DEPENDS ${BUILTINS_DIR}/adt/adt_impl.c ${BUILTINS_DIR}/adt/adt.h
        COMMENT "预编译 builtins/adt/adt_impl.c → adt.a")

    # 生成内嵌资源表（路径列表以 ':' 连接传递，避免 cmake 列表分号转义问题）
    set(EMBED_CPP ${CMAKE_CURRENT_BINARY_DIR}/embedded_builtins.cpp)
    string(REPLACE ";" ":" BUILTIN_RES_JOINED "${BUILTIN_RES}")
    set(BUILTIN_ABS "")
    foreach(r ${BUILTIN_RES})
        list(APPEND BUILTIN_ABS ${BUILTINS_DIR}/${r})
    endforeach()
    add_custom_command(OUTPUT ${EMBED_CPP}
        COMMAND ${CMAKE_COMMAND} -DBASE=${BUILTINS_DIR} "-DRELS=${BUILTIN_RES_JOINED}"
                -DADT_A=${ADT_A} -DOUT=${EMBED_CPP}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_builtins.cmake
        DEPENDS ${ADT_A} ${BUILTIN_ABS}
                ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_builtins.cmake
        COMMENT "生成内嵌资源 embedded_builtins.cpp")
    target_sources(scc PRIVATE ${EMBED_CPP})
    target_compile_definitions(scc PRIVATE SCC_EMBED_BUILTINS)
endif()
