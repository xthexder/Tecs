cmake_minimum_required(VERSION 3.12 FATAL_ERROR)
project(Tecs VERSION 0.5)

# Tell cmake to export a compile_commands.json file
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

# Tell cmake we need C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

## Setup compiler flags for various build types
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS                 "/MP /EHsc")
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

add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(
    ${PROJECT_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}>
)

if(UNIX)
    target_link_libraries(
        ${PROJECT_NAME}
        INTERFACE
            pthread
    )
endif()

target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_17)

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/inc
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}
    FILES_MATCHING PATTERN "*.hh"
)

# Build tests and examples if we are the root project
if("${CMAKE_PROJECT_NAME}" STREQUAL "${PROJECT_NAME}")
    # set(GOOGLETEST_DIR ${EXT_PATH}/googletest/googletest)
    # add_subdirectory(${EXT_PATH}/googletest)
    add_subdirectory(tests)
    add_subdirectory(examples)
ENDIF ()
