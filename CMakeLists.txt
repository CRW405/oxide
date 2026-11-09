cmake_minimum_required(VERSION 3.25)

project(oxide VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Development defaults stay on. Disable these for lean release builds.
option(OXIDE_ENABLE_VALIDATION "Enable Vulkan validation layers" ON)
option(OXIDE_ENABLE_LOGGING "Enable Oxide diagnostic logging" ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 3.3 REQUIRED)

find_path(STB_INCLUDE_DIR NAMES stb_image.h
    PATHS /usr/include/stb /usr/local/include/stb
    PATH_SUFFIXES stb
)
if(NOT STB_INCLUDE_DIR)
    message(FATAL_ERROR "stb_image.h not found!")
endif()

find_path(STB_WRITE_INCLUDE_DIR NAMES stb_image_write.h
    PATHS /usr/include/stb /usr/local/include/stb
    PATH_SUFFIXES stb
)
if(NOT STB_WRITE_INCLUDE_DIR)
    message(FATAL_ERROR "stb_image_write.h not found!")
endif()

function(oxide_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        # Config structs use default member initializers, so partial designated
        # initializers are intentional and should not be treated as errors.
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wno-missing-field-initializers
        )
    endif()
endfunction()

# Oxide itself is a library that example programs (and other projects)
# can link against. Every source file under src/ is part of it; applications
# live in examples/.
file(GLOB_RECURSE OXIDE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
list(APPEND OXIDE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spirv-reflect/spirv_reflect.c"
)

add_library(oxide STATIC ${OXIDE_SOURCES})
add_library(oxide::oxide ALIAS oxide)

target_include_directories(oxide PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(oxide SYSTEM PRIVATE
    ${STB_INCLUDE_DIR}
    ${STB_WRITE_INCLUDE_DIR}
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spirv-reflect"
)
target_link_libraries(oxide PUBLIC Vulkan::Vulkan glfw)
target_compile_features(oxide PUBLIC cxx_std_20)
target_compile_definitions(oxide PUBLIC
    OXIDE_ENABLE_VALIDATION=$<BOOL:${OXIDE_ENABLE_VALIDATION}>
    OXIDE_ENABLE_LOGGING=$<BOOL:${OXIDE_ENABLE_LOGGING}>
)

oxide_enable_warnings(oxide)

message(STATUS "Oxide validation layers: ${OXIDE_ENABLE_VALIDATION}")
message(STATUS "Oxide diagnostic logging: ${OXIDE_ENABLE_LOGGING}")

install(TARGETS oxide EXPORT oxideTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT oxideTargets
    FILE oxideTargets.cmake
    NAMESPACE oxide::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/oxide
)

configure_package_config_file(
    cmake/oxideConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/oxideConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/oxide
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/oxideConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/oxideConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/oxideConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/oxide
)

option(OXIDE_BUILD_EXAMPLES "Build the example programs" ON)
if(OXIDE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
