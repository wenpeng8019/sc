cmake_minimum_required(VERSION 3.16)
project(scc CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 导出 compile_commands.json：供 VS Code IntelliSense 精确解析包含路径，
# 修正 proggraph.cpp 中 #include "proggraph_viewer.inc"（原始字符串内联）误报。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(scc
    src/main.cpp
    src/lexer.cpp
    src/parser.cpp
    src/ast_print.cpp
    src/ast_json.cpp
    src/codegen_c.cpp
    src/codegen_sc.cpp
    # GPU/着色器扩展（syntax-g）后端：.sg 源 → Vulkan-GLSL 文本。与 codegen_c 解耦。
    src/codegen_glsl.cpp
    src/semantic.cpp
    src/cheaders.cpp
    src/remote.cpp
    # 程序结构依赖图（proggraph 子项目）：编译期只读分析，把 AST 烘焙成结构对象依赖图，
    # 从 main/@导出 做激活分析，导出 JSON / 自包含 HTML。与 codegen 解耦。见 proggraph/。
    src/proggraph.cpp
    # 图算法（builtins/graph）：编译期被 scc 引入，对 tok 依赖图（dep…map）做
    # 环检测/拓扑/深度/逆拓扑预计算，结果烘焙为生成代码常量。graph.h 为编译期生产者
    # 与运行时消费者共享的规范接口（spec interface）。图算法独立子模块，亦可被 sc 程序 inc。
    ../builtins/graph/graph_impl.c
)

# builtins 根入 -I：使 src 可 #include "graph/graph.h" 引用图算法规范接口
target_include_directories(scc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../builtins)

target_compile_options(scc PRIVATE -Wall -Wextra)

# 运行时 dlopen libclang（解析 C 头外部描述符）：链接 dl（Linux 需 -ldl，macOS 在 libSystem 内）
target_link_libraries(scc PRIVATE ${CMAKE_DL_LIBS})

# ------------------------------------------------------------
# 异步内核多路复用后端：-DSCC_WITH_UV=ON
#   默认（OFF）：op 层异步内核用 POSIX poll + 自管道 + pthread，零外部依赖、轻量。
#   ON：改用 libuv（epoll/kqueue/IOCP）后端，并让 async 模块叶子原语接入 libuv 现成
#       生态（uv_timer/uv_tcp/... 网络·文件·DNS）。需仓库内 vendor/libuv 已构建出
#       vendor/libuv/build/libuv.a。此宏仅标记编译器二进制，由其在编译 op_impl.c /
#       async_impl.c 时注入 -DSCC_WITH_UV + libuv 头/库（见 main.cpp）。
# ------------------------------------------------------------
option(SCC_WITH_UV "异步内核启用 libuv 多路复用后端（默认 OFF=POSIX poll）" OFF)
if(SCC_WITH_UV)
    target_compile_definitions(scc PRIVATE SCC_WITH_UV)
endif()

# ------------------------------------------------------------
# TLS 后端（builtins/ssl）：-DSCC_SSL_BACKEND=mbedtls|openssl|none（默认 mbedtls）
#   mbedtls ：vendor 源码内置（vendor/mbedtls），按目标工具链现场编静态库并缓存，
#             静态烘进用户二进制 —— 零系统依赖、真正跨平台。默认后端（ssh client 等依赖之）。
#   openssl ：用本地系统预装 OpenSSL（find_package 定位头/库），编译 ssl_impl.c 时由
#             编译器注入 -DSCC_WITH_OPENSSL + OpenSSL 头路径，并向用户程序链接 -lssl -lcrypto。
#   none    ：ssl 模块所有调用安全失败（ssl_backend()==0），零外部依赖（仅用于裁剪 TLS）。
#   本宏仅标记编译器二进制：实际后端选择/头库注入在编译 ssl_impl.c 时进行（见 main.cpp）。
# ------------------------------------------------------------
set(SCC_SSL_BACKEND "mbedtls" CACHE STRING "TLS 后端：mbedtls|openssl|none")
set_property(CACHE SCC_SSL_BACKEND PROPERTY STRINGS mbedtls openssl none)
if(SCC_SSL_BACKEND STREQUAL "openssl")
    find_package(OpenSSL REQUIRED)
    get_filename_component(_SCC_SSL_LIBDIR "${OPENSSL_SSL_LIBRARY}" DIRECTORY)
    target_compile_definitions(scc PRIVATE
        SCC_WITH_OPENSSL
        SCC_OPENSSL_INC="${OPENSSL_INCLUDE_DIR}"
        SCC_OPENSSL_LIBDIR="${_SCC_SSL_LIBDIR}")
    message(STATUS "ssl 后端=openssl（头 ${OPENSSL_INCLUDE_DIR}，库 ${_SCC_SSL_LIBDIR}）")
elseif(SCC_SSL_BACKEND STREQUAL "mbedtls")
    target_compile_definitions(scc PRIVATE SCC_WITH_MBEDTLS)
    message(STATUS "ssl 后端=mbedtls（vendor 源码内置，按目标工具链现场编静态库 + 缓存）")
else()
    message(STATUS "ssl 后端=none（TLS 调用安全失败）")
endif()

# ------------------------------------------------------------
# 远程工具链构建的内置 SSH 后端：-DSCC_WITH_LIBSSH2=ON（默认 ON）
#   ON ：把 vendor/libssh2(+mbedTLS) 源码编进 scc，远程构建无需系统 ssh/scp —— 自带、
#        跨平台。运行期仍可经 ssh_backend=system 改用系统 ssh/scp。
#   OFF：scc 不含 libssh2，远程构建仅支持 ssh_backend=system（依赖系统 OpenSSH）。
# ------------------------------------------------------------
option(SCC_WITH_LIBSSH2 "远程构建内置 libssh2 后端（默认 ON；OFF 仅留 system ssh/scp）" ON)
if(SCC_WITH_LIBSSH2)
    get_filename_component(VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vendor ABSOLUTE)
    if(EXISTS ${VENDOR_DIR}/libssh2/src AND EXISTS ${VENDOR_DIR}/mbedtls/library)
        file(GLOB SCC_MBEDTLS_SRC ${VENDOR_DIR}/mbedtls/library/*.c)
        file(GLOB SCC_LIBSSH2_SRC ${VENDOR_DIR}/libssh2/src/*.c)
        add_library(scc_mbedtls STATIC ${SCC_MBEDTLS_SRC})
        target_include_directories(scc_mbedtls PRIVATE
            ${VENDOR_DIR}/mbedtls/include ${VENDOR_DIR}/mbedtls/library)
        add_library(scc_libssh2 STATIC ${SCC_LIBSSH2_SRC})
        target_compile_definitions(scc_libssh2 PRIVATE LIBSSH2_MBEDTLS HAVE_CONFIG_H)
        target_include_directories(scc_libssh2 PRIVATE
            ${VENDOR_DIR}/libssh2/src ${VENDOR_DIR}/libssh2/include ${VENDOR_DIR}/mbedtls/include)
        target_link_libraries(scc PRIVATE scc_libssh2 scc_mbedtls)
        target_compile_definitions(scc PRIVATE SCC_WITH_LIBSSH2)
        if(WIN32)
            target_link_libraries(scc PRIVATE ws2_32 bcrypt advapi32)
        endif()
        message(STATUS "远程构建 SSH 后端=libssh2（vendor 源码内置，自带不依赖系统 ssh）")
    else()
        message(WARNING "SCC_WITH_LIBSSH2=ON 但缺 vendor/libssh2 或 vendor/mbedtls；"
                        "回退为仅 system ssh/scp 后端")
    endif()
else()
    message(STATUS "远程构建 SSH 后端=system（仅系统 ssh/scp；未内置 libssh2）")
endif()

# ------------------------------------------------------------
# 发行版变体：-DSCC_EMBED_BUILTINS=ON
#   builtins 的 .sc/.h 资源 + 各子项目预编译 .a 内嵌进 scc 二进制，
#   运行时释放到 ~/.cache/scc/builtins-<内容哈希>（见 main.cpp），
#   单二进制发行无需携带 builtins 目录
# ------------------------------------------------------------
option(SCC_EMBED_BUILTINS "内嵌 builtins 资源与预编译子项目静态库（单二进制发行）" OFF)
if(SCC_EMBED_BUILTINS)
    get_filename_component(BUILTINS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../builtins ABSOLUTE)
    # 资源清单：所有 .sc/.h（configure 时收集，builtins 增删文件后需重新 cmake）
    file(GLOB_RECURSE BUILTIN_RES RELATIVE ${BUILTINS_DIR}
         ${BUILTINS_DIR}/*.sc ${BUILTINS_DIR}/*.h)

    # 预编译各子项目默认实现 builtins/x/x_impl.c → x.a
    # （内嵌为 x/x.a，运行时释放后直接参与链接，不再现场编译）
    set(EMBED_LIBS "")
    set(EMBED_LIB_NAMES "")
    file(GLOB BUILTIN_SUBS RELATIVE ${BUILTINS_DIR} ${BUILTINS_DIR}/*)
    foreach(sub ${BUILTIN_SUBS})
        set(IMPL_C ${BUILTINS_DIR}/${sub}/${sub}_impl.c)
        if(EXISTS ${IMPL_C})
            set(SUB_A ${CMAKE_CURRENT_BINARY_DIR}/${sub}.a)
            add_custom_command(OUTPUT ${SUB_A}
                COMMAND cc -O2 -I ${BUILTINS_DIR}/${sub} -I ${BUILTINS_DIR}
                        -c ${IMPL_C} -o ${CMAKE_CURRENT_BINARY_DIR}/${sub}_impl.o
                COMMAND ar rcs ${SUB_A} ${CMAKE_CURRENT_BINARY_DIR}/${sub}_impl.o
                DEPENDS ${IMPL_C} ${BUILTINS_DIR}/${sub}/${sub}.h
                        ${BUILTINS_DIR}/platform.h
                COMMENT "预编译 builtins/${sub}/${sub}_impl.c → ${sub}.a")
            list(APPEND EMBED_LIBS ${SUB_A})
            list(APPEND EMBED_LIB_NAMES ${sub}/${sub}.a)
        endif()
    endforeach()

    # 生成内嵌资源表（路径列表以 ':' 连接传递，避免 cmake 列表分号转义问题）
    set(EMBED_CPP ${CMAKE_CURRENT_BINARY_DIR}/embedded_builtins.cpp)
    string(REPLACE ";" ":" BUILTIN_RES_JOINED "${BUILTIN_RES}")
    string(REPLACE ";" ":" EMBED_LIBS_JOINED "${EMBED_LIBS}")
    string(REPLACE ";" ":" EMBED_LIB_NAMES_JOINED "${EMBED_LIB_NAMES}")
    set(BUILTIN_ABS "")
    foreach(r ${BUILTIN_RES})
        list(APPEND BUILTIN_ABS ${BUILTINS_DIR}/${r})
    endforeach()
    add_custom_command(OUTPUT ${EMBED_CPP}
        COMMAND ${CMAKE_COMMAND} -DBASE=${BUILTINS_DIR} "-DRELS=${BUILTIN_RES_JOINED}"
                "-DLIBS=${EMBED_LIBS_JOINED}" "-DLIB_NAMES=${EMBED_LIB_NAMES_JOINED}"
                -DOUT=${EMBED_CPP}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_builtins.cmake
        DEPENDS ${EMBED_LIBS} ${BUILTIN_ABS}
                ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_builtins.cmake
        COMMENT "生成内嵌资源 embedded_builtins.cpp")
    target_sources(scc PRIVATE ${EMBED_CPP})
    target_compile_definitions(scc PRIVATE SCC_EMBED_BUILTINS)
endif()
