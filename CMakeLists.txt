cmake_minimum_required(VERSION 3.2)
project(sqlite_protobuf)

set(CMAKE_SHARED_LIBRARY_PREFIX "")

add_library(${PROJECT_NAME} SHARED
    src/extension_main.cpp
    src/protobuf_extract.cpp
    src/protobuf_foreach.cpp
    src/protobuf_json.cpp
    src/protodec.cpp
)

#add_executable(test
#  src/main.cpp
#  src/protodec.cpp
#)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 11)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/sqlite3
)
