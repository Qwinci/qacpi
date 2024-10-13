set(QACPI_INTERNAL_OPTIONS
	-fno-stack-protector -ffreestanding -fno-exceptions -fno-rtti
	-fno-threadsafe-statics -fno-strict-aliasing
	-mgeneral-regs-only
	-fno-omit-frame-pointer -mno-red-zone -Wframe-larger-than=4096
)

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
	${CMAKE_CURRENT_LIST_DIR}/src/resources.cpp
	generated/osi.hpp
)
target_compile_options(qacpi_lib PRIVATE ${QACPI_INTERNAL_OPTIONS})
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/src/interpreter.cpp
	PROPERTIES COMPILE_FLAGS -O2)
target_include_directories(qacpi_lib PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" "${CMAKE_CURRENT_LIST_DIR}/src")
target_include_directories(qacpi_lib PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")

add_library(qacpi_events_lib STATIC EXCLUDE_FROM_ALL
	${CMAKE_CURRENT_LIST_DIR}/src/event.cpp
)
target_compile_options(qacpi_events_lib PRIVATE ${QACPI_INTERNAL_OPTIONS})
target_link_libraries(qacpi_events_lib PRIVATE qacpi_lib)

execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_custom_command(OUTPUT "generated/osi.hpp"
	COMMAND "${CMAKE_CURRENT_LIST_DIR}/iasl_helper.py"
	iasl -p "${CMAKE_CURRENT_BINARY_DIR}/osi.aml" "${CMAKE_CURRENT_LIST_DIR}/osi.dsl"
	COMMAND "${CMAKE_CURRENT_LIST_DIR}/gen_osi.py"
	"${CMAKE_CURRENT_BINARY_DIR}/osi.aml" "${CMAKE_CURRENT_BINARY_DIR}/generated/osi.hpp"
	DEPENDS "${CMAKE_CURRENT_LIST_DIR}/osi.dsl"
)

add_library(qacpi::qacpi ALIAS qacpi_lib)
add_library(qacpi::events ALIAS qacpi_events_lib)
