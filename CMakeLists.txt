cmake_minimum_required(VERSION 3.27)
project(qacpi)

set(CMAKE_CXX_STANDARD 20)

add_executable(qacpi
	src/main.cpp
	src/interpreter.cpp
	src/context.cpp
	src/ops.cpp
	src/string.cpp
	src/buffer.cpp
	src/ns.cpp
	src/sync.cpp
	src/handlers.cpp
	src/utils.cpp
	src/logger.cpp
	src/op_region.cpp
	src/resources.cpp
	generated/osi.hpp
)
target_compile_options(qacpi PRIVATE
	-Wall -Wextra -fsanitize=address
)
target_link_options(qacpi PRIVATE
	-fsanitize=address
)
target_include_directories(qacpi PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" include)

add_custom_command(OUTPUT "generated/osi.hpp"
	COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/iasl_helper.py"
		iasl -p "${CMAKE_CURRENT_BINARY_DIR}/osi.aml" "${CMAKE_CURRENT_SOURCE_DIR}/osi.dsl"
	COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/gen_osi.py"
		"${CMAKE_CURRENT_BINARY_DIR}/osi.aml" "${CMAKE_CURRENT_BINARY_DIR}/generated/osi.hpp"
	DEPENDS "osi.dsl"
)

add_custom_target(update_generated
	DEPENDS "generated/osi.hpp"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/generated"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "generated/osi.hpp" "${CMAKE_SOURCE_DIR}/generated/osi.hpp"
)

