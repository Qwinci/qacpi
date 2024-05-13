#pragma once
#include "qacpi/object.hpp"
#include "handlers.hpp"
#include "qacpi/status.hpp"

namespace qacpi {
	struct NamespaceNode;

	struct StringView {
		constexpr StringView() = default;
		constexpr StringView(const char* str, size_t size) : ptr {str}, size {size} {}
		constexpr StringView(const String& str) : ptr {str.data()}, size {str.size()} {} // NOLINT(*-explicit-constructor)
		constexpr StringView(const char* str) : ptr {str}, size {const_strlen(str)} {} // NOLINT(*-explicit-constructor)

		const char* ptr {};
		size_t size {};

		static constexpr size_t const_strlen(const char* str) {
			size_t len = 0;
			while (*str++) ++len;
			return len;
		}
	};

	struct Context {
		constexpr explicit Context(uint8_t revision) : revision {revision} {}

		Status init();

		~Context();

		Status load_table(const uint8_t* aml, uint32_t size);

		Status evaluate(StringView name, ObjectRef& res, ObjectRef* args = nullptr, int arg_count = 0);
		Status evaluate(NamespaceNode* node, StringView name, ObjectRef& res, ObjectRef* args = nullptr, int arg_count = 0);

		Status init_namespace();

		constexpr NamespaceNode* get_root() {
			return root;
		}

	private:
		friend struct Interpreter;
		friend struct OpRegion;

		NamespaceNode* root {};
		NamespaceNode* all_nodes {};
		Mutex* gl {};
		ObjectRef global_locals[8] {
			ObjectRef::empty(), ObjectRef::empty(), ObjectRef::empty(),
			ObjectRef::empty(), ObjectRef::empty(), ObjectRef::empty(),
			ObjectRef::empty(), ObjectRef::empty()
		};
		const RegionSpaceHandler* region_handlers {&PCI_CONFIG_HANDLER};
		NamespaceNode* regions_to_reg {};
		uint64_t timeout_100ns = 10 * 1000 * 1000 * 2;
		uint8_t revision;
	};
}
