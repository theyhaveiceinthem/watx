cmake_minimum_required(VERSION 3.16)
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum supported macOS version" FORCE)
endif()
project(watx VERSION 0.2.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "watx requires a 64-bit target")
endif()
if(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "11.0")
    message(FATAL_ERROR "watx requires macOS 11.0 or newer")
endif()

find_package(Threads REQUIRED)

add_library(watx_core SHARED
    src/lexer/token.cpp
    src/text/syntax.cpp
    src/text/literals.cpp
    src/text/utf8.cpp
    src/model/model.cpp
    src/parser/parser.cpp
    src/parser/instructions.cpp
    src/resolve/source.cpp
    src/validation/validator.cpp
    src/wasm/binary_writer.cpp
    src/wasm/opcodes.cpp
    src/wasm/binary_reader.cpp
    src/runtime/runtime.cpp
    src/runtime/executor.cpp
    src/runtime/host.cpp
    src/runtime/sync.cpp
    src/jit/jit.cpp
    src/jit/platform_x86_64.cpp
    src/jit/platform_arm64.cpp
    src/jit/platform_code_memory.cpp
    src/platform/threads.cpp
)
target_include_directories(watx_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(watx_core PUBLIC Threads::Threads)
set_target_properties(watx_core PROPERTIES
    OUTPUT_NAME watx
    EXPORT_NAME watx
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
add_library(watx::watx ALIAS watx_core)
target_compile_definitions(watx_core PUBLIC WATX_SHARED PRIVATE WATX_BUILD)
if(WIN32)
    target_compile_definitions(watx_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(watx_core PRIVATE m)
endif()
add_executable(watx src/cli/main.cpp src/cli/options.cpp)
target_link_libraries(watx PRIVATE watx_core)
target_compile_definitions(watx PRIVATE WATX_VERSION="${PROJECT_VERSION}")
set_target_properties(watx PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if(APPLE)
    set_target_properties(watx PROPERTIES INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
elseif(UNIX)
    set_target_properties(watx PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()
if(MSVC)
    target_link_options(watx PRIVATE /STACK:8388608)
endif()

foreach(target watx_core watx)
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 98 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -pedantic)
    endif()
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_executable(watx_validator_tests tests/validator_smoke.cpp)
    target_link_libraries(watx_validator_tests PRIVATE watx_core)
    set_target_properties(watx_validator_tests PROPERTIES
        CXX_STANDARD 98 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    add_test(NAME validator COMMAND watx_validator_tests)
    if(CMAKE_GENERATOR_PLATFORM)
        set(WATX_TEST_PROCESSOR "${CMAKE_GENERATOR_PLATFORM}")
    elseif(CMAKE_OSX_ARCHITECTURES)
        set(WATX_TEST_PROCESSOR "${CMAKE_OSX_ARCHITECTURES}")
    else()
        set(WATX_TEST_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    string(TOLOWER "${WATX_TEST_PROCESSOR}" WATX_TEST_PROCESSOR)
    set(WATX_TEST_ARCH "")
    if(WATX_TEST_PROCESSOR MATCHES "^(x86_64|amd64|x64)$")
        set(WATX_TEST_ARCH "x86_64")
    elseif(WATX_TEST_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(WATX_TEST_ARCH "arm64")
    endif()
    add_test(NAME runtime COMMAND ${CMAKE_COMMAND}
        "-DWATX_EXECUTABLE=$<TARGET_FILE:watx>"
        "-DWATX_TEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/runtime-smoke"
        "-DWATX_EXPECTED_ARCH=${WATX_TEST_ARCH}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime_smoke.cmake)
    set_tests_properties(runtime PROPERTIES TIMEOUT 300)
endif()
install(TARGETS watx RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS watx_core EXPORT watxTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/watx DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} PATTERN "cli" EXCLUDE)
install(EXPORT watxTargets FILE watxTargets.cmake NAMESPACE watx::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/watx
)
configure_package_config_file(cmake/watxConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/watxConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/watx
)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/watxConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ExactVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/watxConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/watxConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/watx
)
