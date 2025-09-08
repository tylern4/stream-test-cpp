cmake_minimum_required(VERSION 3.10)
project(DXP VERSION 0.1 LANGUAGES C CXX)
include(FetchContent)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_WARN_DEPRECATED FALSE)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(CMAKE_OSX_ARCHITECTURES ${CMAKE_HOST_SYSTEM_PROCESSOR})
endif()

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 11.2.0
)
FetchContent_Declare(
    libzmq
    GIT_REPOSITORY https://github.com/zeromq/libzmq.git
    GIT_TAG v4.3.5
)
FetchContent_Declare(
    cppzmq
    GIT_REPOSITORY https://github.com/zeromq/cppzmq.git
    GIT_TAG v4.11.0
)
FetchContent_Declare(
    clipp
    GIT_REPOSITORY https://github.com/muellan/clipp.git
    GIT_TAG v1.2.3
)

FetchContent_MakeAvailable(fmt clipp libzmq cppzmq)
include_directories(BEFORE ${clipp_SOURCE_DIR}/include)
include_directories(BEFORE ${fmt_SOURCE_DIR}/include)
set(LIB_LIST libzmq cppzmq fmt::fmt)

file(GLOB PROGRAMS ${PROJECT_SOURCE_DIR}/src/*.cxx)
foreach(prog ${PROGRAMS})
    get_filename_component(progname ${prog} NAME)
    string(REPLACE ".cxx" "" progname ${progname})
    add_executable(${progname} ${prog})
    set_target_properties(${progname} PROPERTIES CXX_STANDARD 17)
    target_link_libraries(${progname} PRIVATE ${LIB_LIST})
    install(TARGETS ${progname} EXPORT ${PROJECT_NAME}Targets RUNTIME DESTINATION bin)
endforeach(prog ${PROGRAMS})