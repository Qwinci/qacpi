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

		[[nodiscard]] constexpr bool operator==(const StringView& other) const {
			if (size != other.size) {
				return false;
			}
			for (size_t i = 0; i < size; ++i) {
				if (ptr[i] != other.ptr[i]) {
					return false;
				}
			}
			return true;
		}

		const char* ptr {};
		size_t size {};

		static constexpr size_t const_strlen(const char* str) {
			size_t len = 0;
			while (*str++) ++len;
			return len;
		}
	};

	enum class LogLevel : uint8_t {
		Error,
		Warning,
		Info,
		Verbose
	};

	inline constexpr bool operator>(LogLevel lhs, LogLevel rhs) {
		return static_cast<int>(lhs) > static_cast<int>(rhs);
	}

	inline constexpr bool operator>=(LogLevel lhs, LogLevel rhs) {
		return static_cast<int>(lhs) >= static_cast<int>(rhs);
	}

	inline constexpr bool operator<(LogLevel lhs, LogLevel rhs) {
		return static_cast<int>(lhs) < static_cast<int>(rhs);
	}

	inline constexpr bool operator<=(LogLevel lhs, LogLevel rhs) {
		return static_cast<int>(lhs) <= static_cast<int>(rhs);
	}

	struct Context {
		constexpr Context(uint8_t revision, LogLevel log_level) : revision {revision}, log_level {log_level} {}

		Status init();

		~Context();

		Status load_table(const uint8_t* aml, uint32_t size);

		Status evaluate(StringView name, ObjectRef& res, ObjectRef* args = nullptr, int arg_count = 0);
		Status evaluate(NamespaceNode* node, StringView name, ObjectRef& res, ObjectRef* args = nullptr, int arg_count = 0);

		Status init_namespace();

		void register_address_space_handler(RegionSpaceHandler* handler);
		void deregister_address_space_handler(RegionSpaceHandler* handler);

		Status discover_nodes(
			NamespaceNode* start,
			const EisaId* ids,
			size_t id_count,
			bool (*fn)(Context& ctx, NamespaceNode* node, void* user_arg),
			void* user_arg);

		Status discover_nodes(
			NamespaceNode* start,
			const StringView* ids,
			size_t id_count,
			bool (*fn)(Context& ctx, NamespaceNode* node, void* user_arg),
			void* user_arg);

		template<typename F>
		Status discover_nodes(NamespaceNode* start, const EisaId* ids, size_t id_count, F f) {
			return discover_nodes(start, ids, id_count, &node_visit_helper<F>, &f);
		}

		template<typename F>
		Status discover_nodes(NamespaceNode* start, const StringView* ids, size_t id_count, F f) {
			return discover_nodes(start, ids, id_count, &node_visit_helper<F>, &f);
		}

		inline NamespaceNode* find_node(NamespaceNode* start, StringView name) {
			if (!start) {
				start = root;
			}

			return create_or_find_node(start, nullptr, name, SearchFlags::Search);
		}

		ObjectRef get_package_element(ObjectRef& pkg, uint32_t index);

		constexpr NamespaceNode* get_root() {
			return root;
		}

	private:
		friend struct Interpreter;
		friend struct OpRegion;

		enum class SearchFlags {
			Create,
			Search
		};

		template<typename F>
		static bool node_visit_helper(Context& ctx, NamespaceNode* node, void* user_arg) {
			auto& fn = *static_cast<remove_reference_t<F>*>(user_arg);
			return fn(ctx, node);
		}

		NamespaceNode* create_or_find_node(NamespaceNode* start, void* method_frame, StringView name, SearchFlags flags);

		NamespaceNode* root {};
		NamespaceNode* all_nodes {};
		Mutex* gl {};
		ObjectRef global_locals[8] {
			ObjectRef::empty(), ObjectRef::empty(), ObjectRef::empty(),
			ObjectRef::empty(), ObjectRef::empty(), ObjectRef::empty(),
			ObjectRef::empty(), ObjectRef::empty()
		};
		RegionSpaceHandler* region_handlers {&PCI_CONFIG_HANDLER};
		NamespaceNode* regions_to_reg {};
		uint64_t timeout_100ns = 10 * 1000 * 1000 * 2;
		uint8_t revision;
		LogLevel log_level;
	};
}
