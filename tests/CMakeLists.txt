include(CheckIPOSupported)
check_ipo_supported(RESULT lto_supported OUTPUT lto_error)

add_executable(${PROJECT_NAME}-benchmark
    benchmark.cpp
)
target_link_libraries(${PROJECT_NAME}-benchmark Tecs)
target_include_directories(
    ${PROJECT_NAME}-benchmark
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(${PROJECT_NAME}-benchmark PRIVATE TECS_UNCHECKED_MODE TECS_ENABLE_PERFORMANCE_TRACING)
if(WIN32)
    # Link winmm library for timeBeginPeriod()
    target_link_libraries(${PROJECT_NAME}-benchmark winmm)
endif()

if(lto_supported)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS "IPO / LTO enabled")
        set_target_properties(${PROJECT_NAME}-benchmark PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
else()
    message(STATUS "IPO / LTO not supported: ${lto_error}")
endif()

TecsGenerateCHeaders(
    TARGET_NAME ${PROJECT_NAME}-benchmark-abi-gen
    HEADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}-benchmark-abi-gen
    ECS_INCLUDE_PATH "test_components.hh"
    ECS_NAME testing::ECS
    SOURCES
        transform_component.cpp
    INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_SOURCE_DIR}
    COMPILE_DEFINITIONS
        TECS_ENABLE_PERFORMANCE_TRACING
)

add_executable(${PROJECT_NAME}-benchmark-abi
    benchmark.cpp
)
target_link_libraries(${PROJECT_NAME}-benchmark-abi ${PROJECT_NAME}-benchmark-abi-gen)
target_include_directories(
    ${PROJECT_NAME}-benchmark-abi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(${PROJECT_NAME}-benchmark-abi PRIVATE BENCHMARK_ABI TECS_UNCHECKED_MODE TECS_ENABLE_PERFORMANCE_TRACING)
if(WIN32)
    # Link winmm library for timeBeginPeriod()
    target_link_libraries(${PROJECT_NAME}-benchmark-abi winmm)
endif()

if(lto_supported)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS "IPO / LTO enabled")
        set_target_properties(${PROJECT_NAME}-benchmark-abi PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
else()
    message(STATUS "IPO / LTO not supported: ${lto_error}")
endif()

# Only build the tracy benchmark if the submodule is populated
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tracy/CMakeLists.txt)
    set(TRACY_NO_EXIT ON CACHE BOOL "" FORCE)
    set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
    add_subdirectory(tracy)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(TracyClient PRIVATE -Wno-unused-result -Wno-unused-function -Wno-maybe-uninitialized)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(TracyClient PRIVATE -Wno-unused-private-field -Wno-unused-function)
    endif()

    add_executable(${PROJECT_NAME}-benchmark-tracy
        benchmark.cpp
    )
    target_link_libraries(${PROJECT_NAME}-benchmark-tracy TracyClient Tecs)
    target_include_directories(
        ${PROJECT_NAME}-benchmark-tracy
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_definitions(
        ${PROJECT_NAME}-benchmark-tracy
        PRIVATE
            TECS_ENABLE_TRACY
            TECS_TRACY_INCLUDE_LOCKS
            TECS_TRACY_INCLUDE_DETAILED_COMMIT
            TECS_UNCHECKED_MODE
    )
    if(WIN32)
        # Link winmm library for timeBeginPeriod()
        target_link_libraries(${PROJECT_NAME}-benchmark-tracy winmm)
    endif()

    if(lto_supported)
        if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
            message(STATUS "IPO / LTO enabled")
            set_target_properties(${PROJECT_NAME}-benchmark-tracy PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
        endif()
    else()
        message(STATUS "IPO / LTO not supported: ${lto_error}")
    endif()

    TecsGenerateCHeaders(
        TARGET_NAME ${PROJECT_NAME}-benchmark-tracy-abi-gen
        HEADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}-benchmark-tracy-abi-gen
        ECS_INCLUDE_PATH "test_components.hh"
        ECS_NAME testing::ECS
        SOURCES
            transform_component.cpp
        LINK_LIBRARIES
            TracyClient
        INCLUDE_DIRECTORIES
            ${CMAKE_CURRENT_SOURCE_DIR}
        COMPILE_DEFINITIONS
            TECS_ENABLE_TRACY
            TECS_TRACY_INCLUDE_LOCKS
            TECS_TRACY_INCLUDE_DETAILED_COMMIT
            TECS_UNCHECKED_MODE
    )

    add_executable(${PROJECT_NAME}-benchmark-tracy-abi
        benchmark.cpp
    )
    target_link_libraries(${PROJECT_NAME}-benchmark-tracy-abi ${PROJECT_NAME}-benchmark-tracy-abi-gen TracyClient)
    target_include_directories(
        ${PROJECT_NAME}-benchmark-tracy-abi
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_definitions(
        ${PROJECT_NAME}-benchmark-tracy-abi
        PRIVATE
            BENCHMARK_ABI
            TECS_ENABLE_TRACY
            TECS_TRACY_INCLUDE_LOCKS
            TECS_TRACY_INCLUDE_DETAILED_COMMIT
            TECS_UNCHECKED_MODE
    )
    if(WIN32)
        # Link winmm library for timeBeginPeriod()
        target_link_libraries(${PROJECT_NAME}-benchmark-tracy-abi winmm)
    endif()

    if(lto_supported)
        if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
            message(STATUS "IPO / LTO enabled")
            set_target_properties(${PROJECT_NAME}-benchmark-tracy-abi PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
        endif()
    else()
        message(STATUS "IPO / LTO not supported: ${lto_error}")
    endif()
endif()

TecsGenerateCHeaders(
    TARGET_NAME ${PROJECT_NAME}-tests-abi-gen
    HEADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}-tests-abi-gen
    ECS_INCLUDE_PATH "test_components.hh"
    ECS_NAME testing::ECS
    SOURCES
        transform_component.cpp
    INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(${PROJECT_NAME}-tests tests.cpp)
target_link_libraries(${PROJECT_NAME}-tests ${PROJECT_NAME})

add_executable(${PROJECT_NAME}-abi_test
    abi_test.cpp
)
target_link_libraries(${PROJECT_NAME}-abi_test ${PROJECT_NAME}-tests-abi-gen)
target_include_directories(
    ${PROJECT_NAME}-abi_test
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(${PROJECT_NAME}-tests-unchecked tests.cpp transform_component.cpp)
target_link_libraries(${PROJECT_NAME}-tests-unchecked ${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME}-tests-unchecked PRIVATE TECS_UNCHECKED_MODE)
