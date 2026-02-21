cmake_minimum_required(VERSION 3.10)
project(VerilogLinter)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(VerilogLinter
    main.cpp
    Lexer.cpp
    Parser.cpp
    Linter.cpp
)

target_include_directories(VerilogLinter PRIVATE ${CMAKE_SOURCE_DIR})