cmake_minimum_required(VERSION 3.12 FATAL_ERROR)
project(Tecs VERSION 0.11.0)

option(TECS_HEADER_ONLY "Use Tecs in header-only library mode (Disables nested transaction detection)" OFF)
option(TECS_ENABLE_PERFORMANCE_TRACING "Include performance trace counters inside Tecs for debugging lock contention" OFF)

# Note: Tracy only supports tracking locks up to 64 threads and will crash the client with more than 64 simultaneous transactions.
option(TECS_ENABLE_TRACY "Add Tracy C++ Profiler hooks for showing transactions and lock contention" OFF)
option(TECS_TRACY_INCLUDE_LOCKS "Include lock contention in Tracy profiling" ON)
option(TECS_TRACY_INCLUDE_DETAILED_COMMIT "Include detailed per-component zones for transaction end in Tracy profiling" ON)

# In UNCHECED mode, calling operations on null or invalid entities will result in undefined behaviour instead of throwing an exception.
# If a program ensures entities are valid itself, removing Tecs' runtime checks can improve performance.
option(TECS_UNCHECKED_MODE "Remove all runtime checks from Tecs (for performance testing)" OFF)

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
    set(CMAKE_CXX_FLAGS                 "-Wall -Werror -fPIC")
    set(CMAKE_CXX_FLAGS_RELEASE         "-O3")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO  "-g3 -ggdb3 -O2")
    set(CMAKE_CXX_FLAGS_DEBUG           "-g3 -ggdb3")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(Apple)?Clang$")
    set(CMAKE_CXX_FLAGS                 "-Wall -Werror -fPIC")
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

if(TECS_ENABLE_TRACY)
    target_compile_definitions(${PROJECT_NAME} INTERFACE TECS_ENABLE_TRACY)
    if(TECS_TRACY_INCLUDE_LOCKS)
        target_compile_definitions(${PROJECT_NAME} INTERFACE TECS_TRACY_INCLUDE_LOCKS)
    endif()
    if(TECS_TRACY_INCLUDE_DETAILED_COMMIT)
        target_compile_definitions(${PROJECT_NAME} INTERFACE TECS_TRACY_INCLUDE_DETAILED_COMMIT)
    endif()
endif()

if(TECS_UNCHECKED_MODE)
    target_compile_definitions(${PROJECT_NAME} INTERFACE TECS_UNCHECKED_MODE)
endif()

include(gen_c_abi.cmake)

if(UNIX AND NOT ANDROID)
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
