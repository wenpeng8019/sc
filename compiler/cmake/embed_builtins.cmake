# ============================================================
# 内嵌 builtins 资源生成脚本（cmake -P 调用，配合 SCC_EMBED_BUILTINS）
#   输入: BASE      = builtins 目录绝对路径
#         RELS      = 冒号分隔的相对路径列表（.sc/.h 资源）
#         LIBS      = 冒号分隔的预编译子项目静态库绝对路径列表
#         LIB_NAMES = 与 LIBS 对位的内嵌相对名（如 adt/adt.a:m/m.a）
#         OUT       = 生成的 .cpp 路径
#   输出: EmbeddedFile 表 + 全部内容的 MD5（运行时作缓存目录名，
#         内容变化自动换目录，无陈旧缓存问题）
# ============================================================
string(REPLACE ":" ";" _rels "${RELS}")
set(_files "")
set(_names "")
foreach(_r IN LISTS _rels)
    list(APPEND _files "${BASE}/${_r}")
    list(APPEND _names "${_r}")
endforeach()
if(NOT "${LIBS}" STREQUAL "")
    string(REPLACE ":" ";" _libs "${LIBS}")
    string(REPLACE ":" ";" _libnames "${LIB_NAMES}")
    list(APPEND _files ${_libs})
    list(APPEND _names ${_libnames})
endif()

set(_decls "")
set(_table "")
set(_allhex "")
list(LENGTH _files _n)
math(EXPR _last "${_n} - 1")
foreach(_i RANGE ${_last})
    list(GET _files ${_i} _f)
    list(GET _names ${_i} _name)
    file(READ "${_f}" _hex HEX)
    string(APPEND _allhex "${_hex}")
    string(LENGTH "${_hex}" _hl)
    math(EXPR _size "${_hl} / 2")
    if(_hex STREQUAL "")
        set(_arr "0")
    else()
        string(REGEX REPLACE "(..)" "0x\\1," _arr "${_hex}")
    endif()
    string(APPEND _decls "static const unsigned char d${_i}[] = {${_arr}};\n")
    string(APPEND _table "    {\"${_name}\", d${_i}, ${_size}},\n")
endforeach()
string(MD5 _hash "${_allhex}")

file(WRITE "${OUT}" "// 自动生成（cmake/embed_builtins.cmake），勿手改：内嵌 builtins 资源
#include <cstddef>
struct EmbeddedFile { const char* path; const unsigned char* data; size_t size; };
${_decls}extern const EmbeddedFile g_embeddedBuiltins[] = {
${_table}};
extern const size_t g_embeddedBuiltinsCount = ${_n};
extern const char* const g_embeddedBuiltinsHash = \"${_hash}\";
")
