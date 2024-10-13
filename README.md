# qacpi
## An ACPI AML interpreter
qacpi is an AML interpreter written in C++20. As of right now it is pretty incomplete
and bare-bones, so it's not recommended to be used for anything serious,
consider using eg. [uACPI](https://github.com/UltraOS/uACPI) instead.

## Usage
- Include lib.cmake (with cmake's `include()`) into your project and link `qacpi_lib` to your executable/library eg.
```cmake
include(qacpi/lib.cmake)

add_executable(myexe main.cpp)
target_link_libraries(myexe PRIVATE qacpi_lib)

```
```cpp
#include "qacpi/context.hpp"
#include "qacpi/ns.hpp"
#include "qacpi/utils.hpp"

int main() {
	uint8_t revision = ...;
	const uint8_t* aml = ...;
	uint32_t aml_size = ...;

	// Create a context that holds the global namespace
	// revision is the revision from the DSDT header
	// log level indicates the minimum verbosity of the logged messages
	qacpi::Context ctx {revision, qacpi::LogLevel::Verbose};

	// Initialize the context
	auto status = ctx.init();
	if (status != qacpi::Status::Success) {
		// print error using qacpi::status_to_str or do something else
	}

	// Load aml into the context (eg. from DSDT or SSDT's)
	status = ctx.load_table(aml_ptr, aml_size);
	// check status like above

	// run STA/INIT for all the objects
	status = ctx.init_namespace();
	// check status

	// Create an empty (null) object to hold the potential return value
	auto ret = qacpi::ObjectRef::empty();

	// Evaluate a method/node in the global namespace
	st = ctx.evaluate("\\MAIN", ret);
	// check status

	if (ret) {
		// ret contains some returned object from the method
	}

	// Evaluate a method/node in the global namespace while passing arguments
	ObjectRef args[2];
	if (!args[0] || !args[1]) {
		// ran out of memory while allocating objects
	}

	qacpi::String str;
	if (!str.init("hello", 5)) {
		// ran out of memory while allocating string
	}

	args[0]->data = uint64_t {1};
	args[1]->data = qacpi::move(str);
	st = ctx.evaluate("\\MAIN", ret, &args, 2);
	// check status and potentially use ret

	// Evaluate a method inside a specific scope (\\_SB_)
	auto root = ctx.get_root();
	auto child = root.get_child("\\_SB_");
	// check that child exists
	st = ctx.evaluate(child, "_STA", ret);

	// discover all nodes starting at the root node
	// whose _HID/_CID matches any of the given strings.
	// value returned from the passed callback tells whether to stop
	// iteration.
	qacpi::StringView match {"QEMU0002"};
	ctx.discover_nodes(ctx.get_root(), &match, 1, [](qacpi::Context& ctx, qacpi::NamespaceNode* node) {
		// do something using node
		return false;
	});

	// this is also predefined in utils.hpp as PCIE_ID.
	qacpi::EisaId pcie_id {"PNP0A08"};
	// discover all nodes starting at the root node
	// whose _HID/_CID matches any of the given eisa ids.
	// value returned from the passed callback tells whether to stop
	// iteration.
	ctx.discover_nodes(ctx.get_root(), &pcie_id, 1, [](qacpi::Context& ctx, qacpi::NamespaceNode* node) {
		// do something using node
		return false;
	});
}

## Credits
- [uACPI](https://github.com/UltraOS/uACPI) for tests and general reference

```
