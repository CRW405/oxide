set(OXIDE_EXAMPLES
    triangle
    sierpinski
    image_texture
    uniform_buffer
    showcase
    compute
    hiss
)

find_package(cglm CONFIG REQUIRED)

find_program(GLSLC_EXECUTABLE glslc)
find_program(GLSLANG_VALIDATOR_EXECUTABLE glslangValidator)

if(GLSLC_EXECUTABLE)
    set(OXIDE_SHADER_COMPILER "${GLSLC_EXECUTABLE}")
    set(OXIDE_SHADER_COMPILER_ARGS "")
elseif(GLSLANG_VALIDATOR_EXECUTABLE)
    set(OXIDE_SHADER_COMPILER "${GLSLANG_VALIDATOR_EXECUTABLE}")
    set(OXIDE_SHADER_COMPILER_ARGS "-V")
else()
    message(FATAL_ERROR "Neither glslc nor glslangValidator was found")
endif()

# Each example is a standalone executable with its own shaders. The compiled
# SPIR-V is placed next to the example binary, and the example is told where
# that directory is through OXIDE_SHADER_DIR. OXIDE_ASSET_DIR points at the
# shared assets directory in the source tree.
foreach(EXAMPLE IN LISTS OXIDE_EXAMPLES)
    set(EXAMPLE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${EXAMPLE}")
    set(EXAMPLE_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/${EXAMPLE}")
    set(EXAMPLE_SHADER_DIR "${EXAMPLE_BINARY_DIR}/shaders")

    file(GLOB EXAMPLE_SHADER_SOURCES CONFIGURE_DEPENDS
        "${EXAMPLE_SOURCE_DIR}/shaders/*.vert"
        "${EXAMPLE_SOURCE_DIR}/shaders/*.frag"
        "${EXAMPLE_SOURCE_DIR}/shaders/*.comp"
    )

    set(EXAMPLE_SHADER_OUTPUTS)
    foreach(SHADER_SOURCE IN LISTS EXAMPLE_SHADER_SOURCES)
        get_filename_component(SHADER_NAME "${SHADER_SOURCE}" NAME)
        set(SHADER_OUTPUT "${EXAMPLE_SHADER_DIR}/${SHADER_NAME}.spv")
        add_custom_command(
            OUTPUT "${SHADER_OUTPUT}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${EXAMPLE_SHADER_DIR}"
            COMMAND "${OXIDE_SHADER_COMPILER}" ${OXIDE_SHADER_COMPILER_ARGS}
                    "${SHADER_SOURCE}" -o "${SHADER_OUTPUT}"
            DEPENDS "${SHADER_SOURCE}"
            COMMENT "Compiling ${EXAMPLE}/${SHADER_NAME}"
            VERBATIM
        )
        list(APPEND EXAMPLE_SHADER_OUTPUTS "${SHADER_OUTPUT}")
    endforeach()

    add_custom_target(${EXAMPLE}_shaders DEPENDS ${EXAMPLE_SHADER_OUTPUTS})

    add_executable(${EXAMPLE} "${EXAMPLE_SOURCE_DIR}/main.cpp")
    add_dependencies(${EXAMPLE} ${EXAMPLE}_shaders)
    target_link_libraries(${EXAMPLE} PRIVATE oxide cglm::cglm)
    target_compile_definitions(${EXAMPLE} PRIVATE
        OXIDE_SHADER_DIR="${EXAMPLE_SHADER_DIR}"
        OXIDE_ASSET_DIR="${PROJECT_SOURCE_DIR}/assets"
    )

    set_target_properties(${EXAMPLE} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${EXAMPLE_BINARY_DIR}"
        VS_DEBUGGER_WORKING_DIRECTORY "${EXAMPLE_BINARY_DIR}"
    )

    oxide_enable_warnings(${EXAMPLE})

    add_custom_target(run_${EXAMPLE}
        COMMAND $<TARGET_FILE:${EXAMPLE}>
        DEPENDS ${EXAMPLE}
        WORKING_DIRECTORY "${EXAMPLE_BINARY_DIR}"
        COMMENT "Running ${EXAMPLE}"
        USES_TERMINAL
    )
endforeach()
