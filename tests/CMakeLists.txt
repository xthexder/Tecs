include(CheckIPOSupported)
check_ipo_supported(RESULT lto_supported OUTPUT lto_error)

add_executable(${PROJECT_NAME}-benchmark
    benchmark.cpp
    transform_component.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/c_abi/Tecs_entity_view.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/c_abi/Tecs_tracing.cc
    ${CMAKE_CURRENT_BINARY_DIR}/Tecs_ecs_gen.cc
    ${CMAKE_CURRENT_BINARY_DIR}/Tecs_entity_gen.cc
    ${CMAKE_CURRENT_BINARY_DIR}/Tecs_lock_gen.cc
)
target_link_libraries(${PROJECT_NAME}-benchmark ${PROJECT_NAME})
target_include_directories(
    ${PROJECT_NAME}-benchmark
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../inc
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(${PROJECT_NAME}-benchmark PRIVATE TECS_ENABLE_PERFORMANCE_TRACING TECS_UNCHECKED_MODE)
if(WIN32)
    # Link winmm library for timeBeginPeriod()
    target_link_libraries(${PROJECT_NAME}-benchmark winmm)
endif()

add_executable(Tecs-gen-ecs ${CMAKE_CURRENT_SOURCE_DIR}/../src/c_abi/impl_gen_ecs.cc)
add_executable(Tecs-gen-entity ${CMAKE_CURRENT_SOURCE_DIR}/../src/c_abi/impl_gen_entity.cc)
add_executable(Tecs-gen-lock ${CMAKE_CURRENT_SOURCE_DIR}/../src/c_abi/impl_gen_lock.cc)
target_include_directories(
    Tecs-gen-ecs
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../inc
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(
    Tecs-gen-entity
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../inc
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(
    Tecs-gen-lock
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../inc
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(Tecs-gen-ecs PRIVATE
    TECS_C_ABI_ECS_INCLUDE="test_components.hh"
    TECS_C_ABI_ECS_NAME=testing::ECS
)
target_compile_definitions(Tecs-gen-entity PRIVATE
    TECS_C_ABI_ECS_INCLUDE="test_components.hh"
    TECS_C_ABI_ECS_NAME=testing::ECS
)
target_compile_definitions(Tecs-gen-lock PRIVATE
    TECS_C_ABI_ECS_INCLUDE="test_components.hh"
    TECS_C_ABI_ECS_NAME=testing::ECS
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Tecs_ecs_gen.cc
    COMMAND Tecs-gen-ecs ${CMAKE_CURRENT_BINARY_DIR}/Tecs_ecs_gen.cc
    DEPENDS Tecs-gen-ecs
)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Tecs_entity_gen.cc
    COMMAND Tecs-gen-entity ${CMAKE_CURRENT_BINARY_DIR}/Tecs_entity_gen.cc
    DEPENDS Tecs-gen-entity
)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Tecs_lock_gen.cc
    COMMAND Tecs-gen-lock ${CMAKE_CURRENT_BINARY_DIR}/Tecs_lock_gen.cc
    DEPENDS Tecs-gen-lock
)

if(lto_supported)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS "IPO / LTO enabled")
        set_property(
            TARGET
                ${PROJECT_NAME}-benchmark
                Tecs-gen-ecs
                Tecs-gen-entity
                Tecs-gen-lock
            PROPERTY
                INTERPROCEDURAL_OPTIMIZATION TRUE
        )
    endif()
else()
    message(STATUS "IPO / LTO not supported: <${lto_error}>")
endif()

# Only build the tracy benchmark if the submodule is populated
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tracy/CMakeLists.txt)
    set(TRACY_NO_EXIT ON CACHE BOOL "" FORCE)
    set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
    add_subdirectory(tracy)
    if(UNIX)
        target_compile_options(TracyClient PRIVATE -Wno-unused-private-field)
    endif()

    add_executable(${PROJECT_NAME}-benchmark-tracy
        benchmark.cpp
        transform_component.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/c_abi/Tecs_entity_view.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/c_abi/Tecs_tracing.cc
        ${CMAKE_CURRENT_BINARY_DIR}/Tecs_ecs_gen2.cc
        ${CMAKE_CURRENT_BINARY_DIR}/Tecs_entity_gen2.cc
        ${CMAKE_CURRENT_BINARY_DIR}/Tecs_lock_gen2.cc
    )
    target_link_libraries(${PROJECT_NAME}-benchmark-tracy ${PROJECT_NAME} TracyClient)
    target_include_directories(
        ${PROJECT_NAME}-benchmark-tracy
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/../inc
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
    
    add_executable(Tecs-gen-ecs2 ${CMAKE_CURRENT_SOURCE_DIR}/../src/c_abi/impl_gen_ecs.cc)
    add_executable(Tecs-gen-entity2 ${CMAKE_CURRENT_SOURCE_DIR}/../src/c_abi/impl_gen_entity.cc)
    add_executable(Tecs-gen-lock2 ${CMAKE_CURRENT_SOURCE_DIR}/../src/c_abi/impl_gen_lock.cc)
    target_link_libraries(Tecs-gen-ecs2 TracyClient)
    target_link_libraries(Tecs-gen-entity2 TracyClient)
    target_link_libraries(Tecs-gen-lock2 TracyClient)
    target_include_directories(
        Tecs-gen-ecs2
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/../inc
            ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_include_directories(
        Tecs-gen-entity2
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/../inc
            ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_include_directories(
        Tecs-gen-lock2
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/../inc
            ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_definitions(Tecs-gen-ecs2 PRIVATE
        TECS_C_ABI_ECS_INCLUDE="test_components.hh"
        TECS_C_ABI_ECS_NAME=testing::ECS
        TECS_ENABLE_TRACY
        TECS_TRACY_INCLUDE_LOCKS
        TECS_TRACY_INCLUDE_DETAILED_COMMIT
    )
    target_compile_definitions(Tecs-gen-entity2 PRIVATE
        TECS_C_ABI_ECS_INCLUDE="test_components.hh"
        TECS_C_ABI_ECS_NAME=testing::ECS
        TECS_ENABLE_TRACY
        TECS_TRACY_INCLUDE_LOCKS
        TECS_TRACY_INCLUDE_DETAILED_COMMIT
    )
    target_compile_definitions(Tecs-gen-lock2 PRIVATE
        TECS_C_ABI_ECS_INCLUDE="test_components.hh"
        TECS_C_ABI_ECS_NAME=testing::ECS
        TECS_ENABLE_TRACY
        TECS_TRACY_INCLUDE_LOCKS
        TECS_TRACY_INCLUDE_DETAILED_COMMIT
    )

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Tecs_ecs_gen2.cc
        COMMAND Tecs-gen-ecs ${CMAKE_CURRENT_BINARY_DIR}/Tecs_ecs_gen2.cc
        DEPENDS Tecs-gen-ecs2
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Tecs_entity_gen2.cc
        COMMAND Tecs-gen-entity2 ${CMAKE_CURRENT_BINARY_DIR}/Tecs_entity_gen2.cc
        DEPENDS Tecs-gen-entity2
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/Tecs_lock_gen2.cc
        COMMAND Tecs-gen-lock2 ${CMAKE_CURRENT_BINARY_DIR}/Tecs_lock_gen2.cc
        DEPENDS Tecs-gen-lock2
    )

    if(lto_supported)
        if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
            message(STATUS "IPO / LTO enabled")
            set_property(
                TARGET
                    ${PROJECT_NAME}-benchmark-tracy
                    Tecs-gen-ecs2
                    Tecs-gen-entity2
                    Tecs-gen-lock2
                PROPERTY
                    INTERPROCEDURAL_OPTIMIZATION TRUE
            )
        endif()
    else()
        message(STATUS "IPO / LTO not supported: <${lto_error}>")
    endif()
endif()

add_executable(${PROJECT_NAME}-tests tests.cpp transform_component.cpp)
target_link_libraries(${PROJECT_NAME}-tests ${PROJECT_NAME})

add_executable(${PROJECT_NAME}-c_abi_test
    c_abi_test.cpp
    transform_component.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/c_abi/Tecs_entity_view.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/c_abi/Tecs_tracing.cc
    ${CMAKE_CURRENT_BINARY_DIR}/Tecs_ecs_gen.cc
    ${CMAKE_CURRENT_BINARY_DIR}/Tecs_entity_gen.cc
    ${CMAKE_CURRENT_BINARY_DIR}/Tecs_lock_gen.cc
)
target_link_libraries(${PROJECT_NAME}-c_abi_test ${PROJECT_NAME})
target_include_directories(
    ${PROJECT_NAME}-c_abi_test
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(${PROJECT_NAME}-tests-unchecked tests.cpp transform_component.cpp)
target_link_libraries(${PROJECT_NAME}-tests-unchecked ${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME}-tests-unchecked PRIVATE TECS_UNCHECKED_MODE)
