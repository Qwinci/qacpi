#pragma once
#include "qacpi/object.hpp"
#include "context.hpp"

namespace qacpi {
	struct NamespaceNode {
		[[nodiscard]] String absolute_path() const;

		[[nodiscard]] constexpr StringView name() const {
			return {_name, 4};
		}

		[[nodiscard]] constexpr NamespaceNode* get_parent() const {
			return parent;
		}

		[[nodiscard]] constexpr ObjectRef& get_object() {
			return object;
		}

		[[nodiscard]] NamespaceNode* get_child(StringView name) const;

		[[nodiscard]] constexpr NamespaceNode** get_children() const {
			return children;
		}

		[[nodiscard]] constexpr size_t get_child_count() const {
			return child_count;
		}

		NamespaceNode* prev_link {};
		NamespaceNode* next_link {};

	private:
		friend struct Context;
		friend struct Interpreter;

		constexpr void* operator new(size_t, void* ptr) {
			return ptr;
		}

		static NamespaceNode* create(const char* name);

		bool add_child(NamespaceNode* child);

		~NamespaceNode();

		char _name[5] {};
		NamespaceNode* parent {};
		NamespaceNode** children {};
		size_t child_count {};
		size_t child_cap {};
		ObjectRef object {ObjectRef::empty()};
		NamespaceNode* link {};
	};
}
