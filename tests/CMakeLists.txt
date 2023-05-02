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

add_executable(${PROJECT_NAME}-tests-unchecked tests.cpp transform_component.cpp)
target_link_libraries(${PROJECT_NAME}-tests-unchecked ${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME}-tests-unchecked PRIVATE TECS_UNCHECKED_MODE)
