# ============================================================
# 内嵌 builtins 资源生成脚本（cmake -P 调用，配合 SCC_EMBED_BUILTINS）
#   输入: BASE      = builtins 目录绝对路径
#         RELS      = 冒号分隔的相对路径列表（.sc/.h/.caps 资源）
#         LIBS      = 冒号分隔的预编译子项目静态库绝对路径列表
#         LIB_NAMES = 与 LIBS 对位的内嵌相对名（如 adt/adt.a:m/m.a）
#         OUT       = 生成的 .cpp 路径
#   输出: 单一预压缩 builtins.tgz blob（cmake -E tar 打包）+ 内容 MD5。
#     · 相比旧的逐文件十六进制表：二进制更小（文本资源高压缩率）、
#       远程构建可直接把 blob 当 bundle 组件上传（零现场打包）、
#       本地释放走一次 tar 解压。
#     · MD5 对「打包前的文件内容」计算（tar 含时间戳不稳定，不可作键）；
#       运行时作释放目录名与远端缓存键，内容变化自动换目录。
# ============================================================
string(REPLACE ":" ";" _rels "${RELS}")

# ---- staging：把资源与预编译库按内嵌相对名布局 ----
get_filename_component(_outdir "${OUT}" DIRECTORY)
set(_stage "${_outdir}/embed_stage")
file(REMOVE_RECURSE "${_stage}")
file(MAKE_DIRECTORY "${_stage}")
set(_allhex "")
foreach(_r IN LISTS _rels)
    get_filename_component(_dir "${_stage}/${_r}" DIRECTORY)
    file(MAKE_DIRECTORY "${_dir}")
    file(COPY_FILE "${BASE}/${_r}" "${_stage}/${_r}")
    file(READ "${BASE}/${_r}" _hex HEX)
    string(APPEND _allhex "${_r}:${_hex};")
endforeach()
if(NOT "${LIBS}" STREQUAL "")
    string(REPLACE ":" ";" _libs "${LIBS}")
    string(REPLACE ":" ";" _libnames "${LIB_NAMES}")
    list(LENGTH _libs _ln)
    math(EXPR _llast "${_ln} - 1")
    foreach(_i RANGE ${_llast})
        list(GET _libs ${_i} _f)
        list(GET _libnames ${_i} _name)
        get_filename_component(_dir "${_stage}/${_name}" DIRECTORY)
        file(MAKE_DIRECTORY "${_dir}")
        file(COPY_FILE "${_f}" "${_stage}/${_name}")
        file(READ "${_f}" _hex HEX)
        string(APPEND _allhex "${_name}:${_hex};")
    endforeach()
endif()
string(MD5 _hash "${_allhex}")

# ---- 打包为 tgz（cmake -E tar：跨平台，无需系统 tar） ----
set(_tgz "${_outdir}/embedded_builtins.tgz")
execute_process(COMMAND ${CMAKE_COMMAND} -E tar czf "${_tgz}" .
                WORKING_DIRECTORY "${_stage}"
                RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "embed_builtins: tar 打包失败")
endif()
file(REMOVE_RECURSE "${_stage}")

# ---- 生成单 blob 数组 ----
file(READ "${_tgz}" _bhex HEX)
string(LENGTH "${_bhex}" _bl)
math(EXPR _bsize "${_bl} / 2")
string(REGEX REPLACE "(..)" "0x\\1," _barr "${_bhex}")

file(WRITE "${OUT}" "// 自动生成（cmake/embed_builtins.cmake），勿手改：内嵌 builtins 预压缩包
#include <cstddef>
static const unsigned char tgz[] = {${_barr}};
extern const unsigned char* const g_embeddedBuiltinsTgz = tgz;
extern const size_t g_embeddedBuiltinsTgzSize = ${_bsize};
extern const char* const g_embeddedBuiltinsHash = \"${_hash}\";
")
