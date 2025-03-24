cmake_minimum_required(VERSION 3.2)
project(sqlite_protobuf)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR})

add_library(${PROJECT_NAME} SHARED
    src/extension_main.cpp
    src/protobuf_extract.cpp
    src/protobuf_foreach.cpp
    src/protobuf_json.cpp
    src/protodec.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/sqlite3
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_executable(test_protodec
  test/test_protodec.cpp
  src/protodec.cpp
)

target_include_directories(test_protodec PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

enable_testing()

add_test(NAME test_protodec 
  COMMAND $<TARGET_FILE:test_protodec>
)

find_package (Python)
add_test(NAME test_sqlite 
  COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/test_sqlite.py 
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)