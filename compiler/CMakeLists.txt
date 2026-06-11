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
)

target_compile_options(scc PRIVATE -Wall -Wextra)
