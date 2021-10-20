cmake_minimum_required(VERSION 3.12 FATAL_ERROR)
project(Tecs VERSION 0.8.0)

option(TECS_HEADER_ONLY "Use Tecs in header-only library mode (Disables nested transaction detection)" OFF)
option(TECS_ENABLE_PERFORMANCE_TRACING "Include performance trace counters inside Tecs for debugging lock contention" OFF)

# Tell cmake to export a compile_commands.json file
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

# A minimum of C++17 is required, but try and use C++20 if available
if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED OFF)
endif()

## Setup compiler flags for various build types
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS                 "/MP /EHsc /W4 /WX")
    set(CMAKE_CXX_FLAGS_RELEASE         "/MD /O2")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO  "/MDd /O2")
    set(CMAKE_CXX_FLAGS_DEBUG           "/MDd /DEBUG /ZI /FC")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS                 "-Wall -Werror")
    set(CMAKE_CXX_FLAGS_RELEASE         "-O3")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO  "-g3 -ggdb3 -O2")
    set(CMAKE_CXX_FLAGS_DEBUG           "-g3 -ggdb3")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS                 "-Wall -Werror")
    set(CMAKE_CXX_FLAGS_RELEASE         "-O3")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO  "-g3 -ggdb3 -O2")
    set(CMAKE_CXX_FLAGS_DEBUG           "-g3 -ggdb3")
else()
    message(FATAL_ERROR "Unknown CXX compiler ${CMAKE_CXX_COMPILER_ID}")
endif()

include(GNUInstallDirs)

if(TECS_HEADER_ONLY)
    add_library(${PROJECT_NAME} INTERFACE)
    target_include_directories(
        ${PROJECT_NAME}
        INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}>
    )
    target_compile_definitions(${PROJECT_NAME} INTERFACE TECS_HEADER_ONLY)
else()
    add_library(${PROJECT_NAME} STATIC src/Tecs.cc)
    target_include_directories(
        ${PROJECT_NAME}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}>
    )
endif()

if(TECS_ENABLE_PERFORMANCE_TRACING)
    target_compile_definitions(${PROJECT_NAME} INTERFACE TECS_ENABLE_PERFORMANCE_TRACING)
endif()

if(UNIX)
    target_link_libraries(
        ${PROJECT_NAME}
        INTERFACE
            pthread
    )
endif()

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/inc
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}
    FILES_MATCHING PATTERN "*.hh"
)

# Build tests and examples if we are the root project
if("${CMAKE_PROJECT_NAME}" STREQUAL "${PROJECT_NAME}")
    add_subdirectory(tests)
    add_subdirectory(examples)
ENDIF ()
