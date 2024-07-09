cmake_minimum_required(VERSION 3.2)
project(sqlite_protobuf)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

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

target_link_libraries(${PROJECT_NAME} 
	${CONAN_LIBS}
)