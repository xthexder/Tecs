add_executable(${PROJECT_NAME}-benchmark benchmark.cpp transform_component.cpp)
target_link_libraries(${PROJECT_NAME}-benchmark ${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME}-benchmark PRIVATE TECS_ENABLE_PERFORMANCE_TRACING TECS_UNCHECKED_MODE)
if(WIN32)
    # Link winmm library for timeBeginPeriod()
    target_link_libraries(${PROJECT_NAME}-benchmark winmm)
endif()

# Only build the tracy benchmark if the submodule is populated
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tracy/CMakeLists.txt)
    set(TRACY_NO_EXIT ON CACHE BOOL "" FORCE)
    set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
    add_subdirectory(tracy)
    if(UNIX)
        target_compile_options(TracyClient PRIVATE -Wno-unused-private-field)
    endif()

    add_executable(${PROJECT_NAME}-benchmark-tracy benchmark.cpp transform_component.cpp)
    target_link_libraries(${PROJECT_NAME}-benchmark-tracy ${PROJECT_NAME} TracyClient)
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
endif()

add_executable(${PROJECT_NAME}-tests tests.cpp transform_component.cpp)
target_link_libraries(${PROJECT_NAME}-tests ${PROJECT_NAME})

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

add_executable(${PROJECT_NAME}-c_abi_test
    c_abi_test.cpp
    transform_component.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/c_abi/Tecs_entity_view.cc
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
