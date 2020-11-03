add_executable(${PROJECT_NAME}-benchmark benchmark.cc)
target_link_libraries(${PROJECT_NAME}-benchmark ${PROJECT_NAME})

add_executable(${PROJECT_NAME}-tests tests.cc)
target_link_libraries(${PROJECT_NAME}-tests ${PROJECT_NAME})
