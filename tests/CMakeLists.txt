include(CheckIPOSupported)
check_ipo_supported(RESULT lto_supported OUTPUT lto_error)

TecsGenerateCHeaders(
    TARGET_NAME ${PROJECT_NAME}-benchmark-c-abi
    ECS_INCLUDE_PATH "test_components.hh"
    ECS_NAME testing::ECS
    INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_SOURCE_DIR}
    COMPILE_DEFINITIONS
        TECS_ENABLE_PERFORMANCE_TRACING
)

add_executable(${PROJECT_NAME}-benchmark
    benchmark.cpp
    transform_component.cpp
)
target_link_libraries(${PROJECT_NAME}-benchmark ${PROJECT_NAME} ${PROJECT_NAME}-benchmark-c-abi)
target_include_directories(
    ${PROJECT_NAME}-benchmark
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(${PROJECT_NAME}-benchmark PRIVATE TECS_ENABLE_PERFORMANCE_TRACING TECS_UNCHECKED_MODE)
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

# Only build the tracy benchmark if the submodule is populated
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tracy/CMakeLists.txt)
    set(TRACY_NO_EXIT ON CACHE BOOL "" FORCE)
    set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
    add_subdirectory(tracy)
    if(UNIX)
        target_compile_options(TracyClient PRIVATE -Wno-unused-private-field)
    endif()

    TecsGenerateCHeaders(
        TARGET_NAME ${PROJECT_NAME}-benchmark-tracy-c-abi
        ECS_INCLUDE_PATH "test_components.hh"
        ECS_NAME testing::ECS
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

    add_executable(${PROJECT_NAME}-benchmark-tracy
        benchmark.cpp
        transform_component.cpp
    )
    target_link_libraries(${PROJECT_NAME}-benchmark-tracy ${PROJECT_NAME} ${PROJECT_NAME}-benchmark-tracy-c-abi TracyClient)
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
endif()

TecsGenerateCHeaders(
    TARGET_NAME ${PROJECT_NAME}-tests-c-abi
    ECS_INCLUDE_PATH "test_components.hh"
    ECS_NAME testing::ECS
    INCLUDE_DIRECTORIES
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(${PROJECT_NAME}-tests tests.cpp transform_component.cpp)
target_link_libraries(${PROJECT_NAME}-tests ${PROJECT_NAME})

add_executable(${PROJECT_NAME}-c_abi_test
    c_abi_test.cpp
    transform_component.cpp
)
target_link_libraries(${PROJECT_NAME}-c_abi_test ${PROJECT_NAME} ${PROJECT_NAME}-tests-c-abi)
target_include_directories(
    ${PROJECT_NAME}-c_abi_test
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(${PROJECT_NAME}-tests-unchecked tests.cpp transform_component.cpp)
target_link_libraries(${PROJECT_NAME}-tests-unchecked ${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME}-tests-unchecked PRIVATE TECS_UNCHECKED_MODE)
