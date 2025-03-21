cmake_minimum_required(VERSION 3.2)
project(sqlite_protobuf)

set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR})

add_library(${PROJECT_NAME} SHARED
    src/extension_main.cpp
    src/protobuf_extract.cpp
    src/protobuf_foreach.cpp
    src/protobuf_json.cpp
    src/protodec.cpp
)

add_executable(test_protodec
  test/test_protodec.cpp
  src/protodec.cpp
)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 11)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/sqlite3
)

enable_testing()
add_test(NAME test_protodec 
  COMMAND $<TARGET_FILE:test_protodec>
)
#foreach(i RANGE 0 13)
#    add_test(NAME test_protodec_${i} COMMAND $<TARGET_FILE:test_protodec> ${i})
#endforeach()

find_package (Python)
add_test(NAME test_sqlite 
  COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/test_sqlite.py 
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)