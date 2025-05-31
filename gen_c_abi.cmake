function(TecsGenerateCHeaders)
    set(options OBJECT_TARGET)
    set(oneValueArgs TARGET_NAME ECS_INCLUDE_PATH ECS_C_INCLUDE_PATH ECS_NAME COMPONENT_TYPE_PREFIX)
    set(multiValueArgs SOURCES LINK_LIBRARIES INCLUDE_DIRECTORIES COMPILE_DEFINITIONS)
    cmake_parse_arguments(PARSE_ARGV 0 arg "${options}" "${oneValueArgs}" "${multiValueArgs}")

    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported OUTPUT lto_error)

    set(TECS_PROJECT_ROOT ${CMAKE_CURRENT_FUNCTION_LIST_DIR})

    add_executable(${arg_TARGET_NAME}-codegen ${TECS_PROJECT_ROOT}/src/c_abi/codegen/gen_main.cc)
    target_link_libraries(${arg_TARGET_NAME}-codegen PRIVATE ${arg_LINK_LIBRARIES})
    target_include_directories(
        ${arg_TARGET_NAME}-codegen
        PRIVATE
            ${TECS_PROJECT_ROOT}/inc
            ${arg_INCLUDE_DIRECTORIES}
    )
    target_compile_definitions(${arg_TARGET_NAME}-codegen PRIVATE
        TECS_C_ABI_ECS_NAME=${arg_ECS_NAME}
        TECS_C_ABI_TYPE_PREFIX="${arg_COMPONENT_TYPE_PREFIX}"
        TECS_SHARED_INTERNAL
        ${arg_COMPILE_DEFINITIONS}
    )

    if(DEFINED arg_ECS_INCLUDE_PATH)
        target_compile_definitions(${arg_TARGET_NAME}-codegen PRIVATE
            TECS_C_ABI_ECS_INCLUDE="${arg_ECS_INCLUDE_PATH}"
        )
    endif()

    if(DEFINED arg_ECS_C_INCLUDE_PATH)
        target_compile_definitions(${arg_TARGET_NAME}-codegen PRIVATE
            TECS_C_ABI_ECS_C_INCLUDE="${arg_ECS_C_INCLUDE_PATH}"
        )
    endif()

    string(REPLACE "-" "_" OUTPUT_PREFIX_NAME ${arg_TARGET_NAME})

    add_custom_command(
        OUTPUT
            ${CMAKE_CURRENT_BINARY_DIR}/include/c_abi/${OUTPUT_PREFIX_NAME}_lock_gen.h
            ${CMAKE_CURRENT_BINARY_DIR}/include/c_abi/${OUTPUT_PREFIX_NAME}_entity_gen.h
            ${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_PREFIX_NAME}_ecs_gen.cc
            ${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_PREFIX_NAME}_entity_gen.cc
            ${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_PREFIX_NAME}_lock_gen.cc
        COMMAND
            ${arg_TARGET_NAME}-codegen
            ${CMAKE_CURRENT_BINARY_DIR}/include/c_abi/${OUTPUT_PREFIX_NAME}_lock_gen.h
            ${CMAKE_CURRENT_BINARY_DIR}/include/c_abi/${OUTPUT_PREFIX_NAME}_entity_gen.h
            ${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_PREFIX_NAME}_ecs_gen.cc
            ${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_PREFIX_NAME}_entity_gen.cc
            ${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_PREFIX_NAME}_lock_gen.cc
        DEPENDS ${arg_TARGET_NAME}-codegen
    )

    set(BUILD_FILE_LIST
        ${TECS_PROJECT_ROOT}/src/c_abi/Tecs_entity_view.cc
        ${TECS_PROJECT_ROOT}/src/c_abi/Tecs_tracing.cc
        ${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_PREFIX_NAME}_ecs_gen.cc
        ${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_PREFIX_NAME}_entity_gen.cc
        ${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_PREFIX_NAME}_lock_gen.cc
        ${arg_SOURCES}
    )

    if(arg_OBJECT_TARGET)
        add_library(${arg_TARGET_NAME} OBJECT ${BUILD_FILE_LIST})
    else()
        add_library(${arg_TARGET_NAME} SHARED ${BUILD_FILE_LIST})
    endif()
    target_link_libraries(${arg_TARGET_NAME} PRIVATE Tecs ${arg_LINK_LIBRARIES})
    target_include_directories(
        ${arg_TARGET_NAME}
        PUBLIC
            ${TECS_PROJECT_ROOT}/inc
            ${CMAKE_CURRENT_BINARY_DIR}/include
        PRIVATE
            ${arg_INCLUDE_DIRECTORIES}
    )
    target_compile_definitions(${arg_TARGET_NAME} PRIVATE TECS_SHARED_INTERNAL ${arg_COMPILE_DEFINITIONS})
    if(lto_supported)
        if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
            message(STATUS "IPO / LTO enabled")
            set_target_properties(${arg_TARGET_NAME} PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
        endif()
    else()
        message(STATUS "IPO / LTO not supported: ${lto_error}")
    endif()
endfunction()
