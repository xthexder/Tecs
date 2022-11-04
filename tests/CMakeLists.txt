add_executable(${PROJECT_NAME}-benchmark benchmark.cpp transform_component.cpp)
target_link_libraries(${PROJECT_NAME}-benchmark ${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME}-benchmark PRIVATE TECS_ENABLE_PERFORMANCE_TRACING)

set(TRACY_NO_EXIT ON CACHE BOOL "" FORCE)
set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
add_subdirectory(tracy)

add_executable(${PROJECT_NAME}-benchmark-tracy benchmark.cpp transform_component.cpp)
target_link_libraries(${PROJECT_NAME}-benchmark-tracy ${PROJECT_NAME} TracyClient)
target_compile_definitions(${PROJECT_NAME}-benchmark-tracy PRIVATE TECS_ENABLE_TRACY)

if(WIN32)
    # Link winmm library for timeBeginPeriod()
    target_link_libraries(${PROJECT_NAME}-benchmark winmm)
    target_link_libraries(${PROJECT_NAME}-benchmark-tracy winmm)
endif()

add_executable(${PROJECT_NAME}-tests tests.cpp transform_component.cpp)
target_link_libraries(${PROJECT_NAME}-tests ${PROJECT_NAME})
