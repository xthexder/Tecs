add_executable(${PROJECT_NAME}-benchmark-compare benchmark.cpp)
target_link_libraries(${PROJECT_NAME}-benchmark-compare ${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME}-benchmark-compare PRIVATE TECS_HEADER_ONLY)
if(WIN32)
    # Link winmm library for timeBeginPeriod()
    target_link_libraries(${PROJECT_NAME}-benchmark-compare winmm)
endif()

