add_library(qacpi_lib STATIC
	${CMAKE_CURRENT_LIST_DIR}/src/interpreter.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/context.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/ops.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/string.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/buffer.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/ns.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/sync.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/handlers.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/utils.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/logger.cpp
	${CMAKE_CURRENT_LIST_DIR}/src/op_region.cpp
	generated/osi.hpp
)
target_include_directories(qacpi_lib PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" "${CMAKE_CURRENT_LIST_DIR}/src")
target_include_directories(qacpi_lib PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")

execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_custom_command(OUTPUT "generated/osi.hpp"
	COMMAND "${CMAKE_CURRENT_LIST_DIR}/tests/iasl_helper.py"
	iasl -p "${CMAKE_CURRENT_BINARY_DIR}/osi.aml" "${CMAKE_CURRENT_LIST_DIR}/osi.dsl"
	COMMAND "${CMAKE_CURRENT_LIST_DIR}/gen_osi.py"
	"${CMAKE_CURRENT_BINARY_DIR}/osi.aml" "${CMAKE_CURRENT_BINARY_DIR}/generated/osi.hpp"
	DEPENDS "${CMAKE_CURRENT_LIST_DIR}/osi.dsl"
)
