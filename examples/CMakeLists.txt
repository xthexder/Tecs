add_executable(${PROJECT_NAME}-basic-example basic_example.cpp)
target_link_libraries(${PROJECT_NAME}-basic-example ${PROJECT_NAME})

add_executable(${PROJECT_NAME}-tracing-example tracing_example.cpp)
target_link_libraries(${PROJECT_NAME}-tracing-example ${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME}-tracing-example PRIVATE TECS_ENABLE_PERFORMANCE_TRACING)
