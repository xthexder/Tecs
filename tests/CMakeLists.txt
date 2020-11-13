add_executable(${PROJECT_NAME}-benchmark benchmark.cpp test_components.cpp)
target_link_libraries(${PROJECT_NAME}-benchmark ${PROJECT_NAME})

add_executable(${PROJECT_NAME}-tests tests.cpp test_components.cpp)
target_link_libraries(${PROJECT_NAME}-tests ${PROJECT_NAME})
