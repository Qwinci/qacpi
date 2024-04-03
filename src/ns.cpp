#include "qacpi/ns.hpp"

static bool name_cmp(const char* a, const char* b) {
	return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

namespace qacpi {
	String NamespaceNode::absolute_path() const {
		uint32_t size = 1;

		auto* tmp = this;
		while (tmp->parent) {
			size += 4;
			tmp = tmp->parent;
			if (tmp && tmp->_name[0]) {
				size += 1;
			}
			else {
				break;
			}
		}

		String str;
		if (!str.init_with_size(size)) {
			return str;
		}

		auto* data = str.data();
		*data = '\\';
		data += size;
		auto* node = this;
		while (node->parent) {
			data -= 4;
			memcpy(data, node->_name, 4);
			node = node->parent;
			if (node->parent) {
				*--data = '.';
			}
		}
		return str;
	}

	NamespaceNode* NamespaceNode::get_child(StringView name) const {
		for (size_t i = 0; i < child_count; ++i) {
			auto child = children[i];
			if (name_cmp(child->_name, name.ptr)) {
				return child;
			}
		}

		return nullptr;
	}

	NamespaceNode* NamespaceNode::create(const char* name) {
		auto* ptr = qacpi_os_malloc(sizeof(NamespaceNode));
		if (!ptr) {
			return nullptr;
		}
		auto* node = new (ptr) NamespaceNode {};
		memcpy(node->_name, name, 4);
		return node;
	}

	bool NamespaceNode::add_child(NamespaceNode* child) {
		if (child_count == child_cap) {
			size_t new_cap = child_cap < 8 ? 8 : child_cap * 2;
			auto* new_ptr = static_cast<NamespaceNode**>(qacpi_os_malloc(new_cap * sizeof(NamespaceNode*)));
			if (!new_ptr) {
				return false;
			}
			memcpy(new_ptr, children, child_count * sizeof(NamespaceNode*));
			if (children) {
				qacpi_os_free(children, child_cap * sizeof(NamespaceNode*));
			}
			children = new_ptr;
			child_cap = new_cap;
		}

		children[child_count++] = child;
		return true;
	}

	NamespaceNode::~NamespaceNode() {
		if (children) {
			qacpi_os_free(children, child_cap * sizeof(NamespaceNode*));
		}
	}
}
