cmake_minimum_required(VERSION 3.16)

project(TestRunner C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../lib.cmake)

foreach(CONF_TYPE ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONF_TYPE} CONF_TYPE)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONF_TYPE} ${CMAKE_BINARY_DIR})
endforeach(CONF_TYPE ${CMAKE_CONFIGURATION_TYPES})

add_executable(
    test-runner
    test_runner.cpp
    helpers.cpp
    interface_impl.cpp
    resource_tests.cpp
)
target_link_libraries(test-runner PRIVATE qacpi::qacpi qacpi::events)

if (MSVC)
    # Address sanitizer on MSVC depends on a dynamic library that is not present in
    # PATH by default. Lets just not enable it here.
    target_compile_options(
        test-runner
        PRIVATE
        /W3 /WX
        /wd4200 /wd4267 /wd4244
    )
else ()
    target_compile_options(
        test-runner
        PRIVATE
        -fsanitize=address,undefined -g3 -Wall -Wextra -Werror
    )
    target_link_options(
        test-runner
        PRIVATE
        -fsanitize=address,undefined -g3
    )
endif ()
