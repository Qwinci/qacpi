#include "qacpi/context.hpp"
#include "interpreter.hpp"
#include "qacpi/ns.hpp"
#include "logger.hpp"
#include "osi.hpp"

using namespace qacpi;

Status Context::init() {
	auto create_predefined_node = [&](const char* name, ObjectRef obj) {
		auto node = NamespaceNode::create(name);
		if (!node) {
			return Status::NoMemory;
		}
		node->link = all_nodes;
		all_nodes = node;
		node->object = move(obj);
		node->object->node = node;
		if (!root->add_child(node)) {
			return Status::NoMemory;
		}
		node->parent = root;
		return Status::Success;
	};

	root = NamespaceNode::create("\0\0\0");
	if (!root) {
		return Status::NoMemory;
	}
	all_nodes = root;

	ObjectRef gl_obj;
	if (!gl_obj) {
		return Status::NoMemory;
	}
	Mutex mutex;
	if (!mutex.init()) {
		return Status::NoMemory;
	}
	gl_obj->data = move(mutex);
	if (auto status = create_predefined_node("_GL_", move(gl_obj)); status != Status::Success) {
		return status;
	}

	gl = &root->get_child("_GL_")->object->get_unsafe<Mutex>();

	ObjectRef osi_obj;
	if (!osi_obj) {
		return Status::NoMemory;
	}
	osi_obj->data = Method {
		.aml = OSI_DATA,
		.mutex {},
		.size = OSI_SIZE,
		.arg_count = 1,
		.serialized = false
	};
	if (auto status = create_predefined_node("_OSI", move(osi_obj)); status != Status::Success) {
		return status;
	}

	ObjectRef sb_obj;
	if (!sb_obj) {
		return Status::NoMemory;
	}
	sb_obj->data = Device {};
	if (auto status = create_predefined_node("_SB_", move(sb_obj)); status != Status::Success) {
		return status;
	}

	ObjectRef gpe_obj;
	if (!gpe_obj) {
		return Status::NoMemory;
	}
	gpe_obj->data = Device {};
	if (auto status = create_predefined_node("_GPE", move(gpe_obj)); status != Status::Success) {
		return status;
	}

	ObjectRef pr_obj;
	if (!pr_obj) {
		return Status::NoMemory;
	}
	pr_obj->data = Device {};
	if (auto status = create_predefined_node("_PR_", move(pr_obj)); status != Status::Success) {
		return status;
	}

	ObjectRef tz_obj;
	if (!tz_obj) {
		return Status::NoMemory;
	}
	tz_obj->data = Device {};
	if (auto status = create_predefined_node("_TZ_", move(tz_obj)); status != Status::Success) {
		return status;
	}

	String os_name;
	if (!os_name.init("Microsoft Windows NT", sizeof("Microsoft Windows NT") - 1)) {
		return Status::NoMemory;
	}

	ObjectRef os_obj;
	if (!os_obj) {
		return Status::NoMemory;
	}
	os_obj->data = move(os_name);
	if (auto status = create_predefined_node("_OS_", move(os_obj)); status != Status::Success) {
		return status;
	}

	ObjectRef rev_obj;
	if (!rev_obj) {
		return Status::NoMemory;
	}
	rev_obj->data = uint64_t {2};
	if (auto status = create_predefined_node("_REV", move(rev_obj)); status != Status::Success) {
		return status;
	}

	return Status::Success;
}

Context::~Context() {
	auto* node = all_nodes;
	while (node) {
		auto* next = node->link;
		node->~NamespaceNode();
		qacpi_os_free(node, sizeof(NamespaceNode));
		node = next;
	}
}

Status Context::load_table(const uint8_t* aml, uint32_t size) {
	auto* mem = qacpi_os_malloc(sizeof(Interpreter));
	if (!mem) {
		return Status::NoMemory;
	}
	auto* interp = construct<Interpreter>(mem, this, static_cast<uint8_t>(revision >= 2 ? 8 : 4));

	auto status = interp->execute(aml, size);

	interp->~Interpreter();
	qacpi_os_free(mem, sizeof(Interpreter));

	return status;
}

Status Context::evaluate(StringView name, ObjectRef& res, ObjectRef* args, int arg_count) {
	auto* node = create_or_find_node(root, nullptr, name, SearchFlags::Search);
	if (!node) {
		return Status::MethodNotFound;
	}
	if (!node->object) {
		LOG << "qacpi internal error in Context::evaluate, node->object is null" << endlog;
		return Status::InternalError;
	}
	if (node->object->get<Method>()) {
		auto* mem = qacpi_os_malloc(sizeof(Interpreter));
		if (!mem) {
			return Status::NoMemory;
		}

		auto* interp = construct<Interpreter>(mem, this, static_cast<uint8_t>(revision >= 2 ? 8 : 4));

		auto status = interp->invoke_method(node, res, args, arg_count);

		interp->~Interpreter();
		qacpi_os_free(mem, sizeof(Interpreter));

		return status;
	}
	else {
		res = node->object;
		return Status::Success;
	}
}

Status Context::evaluate(NamespaceNode* node, StringView name, ObjectRef& res, ObjectRef* args, int arg_count) {
	if (!node) {
		return Status::MethodNotFound;
	}

	node = node->get_child(name);
	if (!node) {
		return Status::MethodNotFound;
	}

	if (!node->object) {
		LOG << "qacpi internal error in Context::evaluate, node->object is null" << endlog;
		return Status::InternalError;
	}
	if (node->object->get<Method>()) {
		auto* mem = qacpi_os_malloc(sizeof(Interpreter));
		if (!mem) {
			return Status::NoMemory;
		}
		auto* interp = construct<Interpreter>(mem, this, static_cast<uint8_t>(revision >= 2 ? 8 : 4));

		auto status = interp->invoke_method(node, res, args, arg_count);

		interp->~Interpreter();
		qacpi_os_free(mem, sizeof(Interpreter));

		return status;
	}
	else {
		res = node->object;
		return Status::Success;
	}
}

static constexpr uint32_t DEVICE_PRESENT = 1 << 0;
static constexpr uint32_t DEVICE_FUNCTIONING = 1 << 3;

Status Context::init_namespace() {
	//LOG << "qacpi: Running _STA/_INI" << endlog;

	auto* reg_region = regions_to_reg;
	while (reg_region) {
		auto& region = reg_region->object->get_unsafe<OpRegion>();
		auto status = region.run_reg();
		if (status == Status::Success) {
			if (reg_region->prev_link) {
				reg_region->prev_link->next_link = reg_region->next_link;
			}
			else {
				regions_to_reg = reg_region->next_link;
			}
			if (reg_region->next_link) {
				reg_region->next_link->prev_link = reg_region->prev_link;
			}

			reg_region = reg_region->next_link;
		}
		else {
			reg_region = reg_region->next_link;
		}
	}

	SmallVec<NamespaceNode*, 32> stack;
	if (!stack.push(root)) {
		return Status::NoMemory;
	}

	auto res = ObjectRef::empty();
	while (!stack.is_empty()) {
		auto node = stack.pop();

		auto status = evaluate(node, "_STA", res);

		bool run_ini = node->_name[0] == 0;
		bool examine_children = node->_name[0] == 0;

		if (status == Status::Success) {
			auto value = res->get_unsafe<uint64_t>();
			if (!(value & DEVICE_PRESENT) && (value & DEVICE_FUNCTIONING)) {
				examine_children = true;
			}
			else if (value & DEVICE_PRESENT) {
				run_ini = true;
				examine_children = true;
			}
		}
		else if (status != Status::MethodNotFound) {
			LOG << "qacpi: error while running _STA for " << node->name() << endlog;
		}
		else {
			if (node->object && node->object->is_device()) {
				run_ini = true;
			}
			examine_children = true;
		}

		if (run_ini) {
			status = evaluate(node, "_INI", res);
			if (status != Status::Success && status != Status::MethodNotFound) {
				LOG << "qacpi: error while running _INI for " << node->name() << endlog;
			}
		}

		if (examine_children) {
			for (size_t i = 0; i < node->child_count; ++i) {
				if (!stack.push(node->children[i])) {
					return Status::NoMemory;
				}
			}
		}
	}

	return Status::Success;
}

void Context::register_address_space_handler(RegionSpaceHandler* handler) {
	handler->prev = nullptr;
	handler->next = region_handlers;
	if (handler->next) {
		handler->next->prev = handler;
	}
	region_handlers = handler;

	auto* reg_region = regions_to_reg;
	while (reg_region) {
		auto& region = reg_region->object->get_unsafe<OpRegion>();
		if (region.space == handler->id) {
			auto status = region.run_reg();
			if (status == Status::Success) {
				if (reg_region->prev_link) {
					reg_region->prev_link->next_link = reg_region->next_link;
				}
				else {
					regions_to_reg = reg_region->next_link;
				}
				if (reg_region->next_link) {
					reg_region->next_link->prev_link = reg_region->prev_link;
				}

				reg_region = reg_region->next_link;
			}
			else {
				reg_region = reg_region->next_link;
			}
		}
	}
}

void Context::deregister_address_space_handler(RegionSpaceHandler* handler) {
	if (handler->prev) {
		handler->prev->next = handler->next;
	}
	else {
		region_handlers = handler->next;
	}
	if (handler->next) {
		handler->next->prev = handler->prev;
	}
}

Status Context::discover_nodes(
	NamespaceNode* start,
	const EisaId* ids,
	size_t id_count,
	bool (*fn)(Context&, NamespaceNode*, void*),
	void* user_arg) {
	SmallVec<NamespaceNode*, 8> stack;
	if (!stack.push(start)) {
		return Status::NoMemory;
	}

	while (!stack.is_empty()) {
		auto node = stack.pop();

		auto res = ObjectRef::empty();
		auto status = evaluate(node, "_HID", res);

		EisaId hid_id;
		EisaId cid_id;

		bool matched = false;

		if (status == Status::Success) {
			if (auto str = res->get<String>()) {
				if (str->size() >= 6) {
					hid_id = EisaId {str->data(), str->size()};
				}
			}
			else if (auto integer = res->get<uint64_t>()) {
				hid_id = EisaId::decode(*integer);
			}
		}
		else if (status != Status::MethodNotFound) {
			return status;
		}

		for (size_t i = 0; i < id_count; ++i) {
			if (ids[i] == hid_id) {
				if (fn(*this, node, user_arg)) {
					return Status::Success;
				}
				matched = true;
			}
		}

		status = evaluate(node, "_CID", res);
		if (status == Status::Success) {
			if (auto str = res->get<String>()) {
				if (str->size() >= 6) {
					cid_id = EisaId {str->data(), str->size()};
				}
			}
			else if (auto integer = res->get<uint64_t>()) {
				cid_id = EisaId::decode(*integer);
			}
			else if (auto pkg = res->get<Package>()) {
				for (uint32_t i = 0; i < pkg->data->element_count; ++i) {
					auto& element = pkg->data->elements[i];
					if ((str = element->get<String>())) {
						if (str->size() >= 6) {
							cid_id = EisaId {str->data(), str->size()};
						}
					}
					else if ((integer = element->get<uint64_t>())) {
						cid_id = EisaId::decode(*integer);
					}

					for (size_t j = 0; j < id_count; ++j) {
						if (ids[j] == cid_id) {
							if (fn(*this, node, user_arg)) {
								return Status::Success;
							}
						}
					}
				}

				cid_id = {};
			}
		}
		else if (status != Status::MethodNotFound) {
			return status;
		}

		if (!matched) {
			for (size_t i = 0; i < id_count; ++i) {
				if (ids[i] == cid_id) {
					if (fn(*this, node, user_arg)) {
						return Status::Success;
					}
				}
			}
		}

		for (size_t i = 0; i < node->child_count; ++i) {
			if (!stack.push(node->children[i])) {
				return Status::NoMemory;
			}
		}
	}

	return Status::Success;
}

Status Context::discover_nodes(
	NamespaceNode* start,
	const StringView* ids,
	size_t id_count,
	bool (*fn)(Context&, NamespaceNode*, void*),
	void* user_arg) {
	SmallVec<NamespaceNode*, 8> stack;
	if (!stack.push(start)) {
		return Status::NoMemory;
	}

	while (!stack.is_empty()) {
		auto node = stack.pop();

		auto res = ObjectRef::empty();
		auto status = evaluate(node, "_HID", res);

		bool matched = false;

		if (status == Status::Success) {
			if (auto str = res->get<String>()) {
				for (size_t i = 0; i < id_count; ++i) {
					if (ids[i] == *str) {
						if (fn(*this, node, user_arg)) {
							return Status::Success;
						}
						matched = true;
						break;
					}
				}
			}
		}
		else if (status != Status::MethodNotFound) {
			return status;
		}

		if (!matched) {
			status = evaluate(node, "_CID", res);
			if (status == Status::Success) {
				if (auto str = res->get<String>()) {
					for (size_t i = 0; i < id_count; ++i) {
						if (ids[i] == *str) {
							if (fn(*this, node, user_arg)) {
								return Status::Success;
							}
							break;
						}
					}
				}
				else if (auto pkg = res->get<Package>()) {
					for (uint32_t i = 0; i < pkg->data->element_count; ++i) {
						auto& element = pkg->data->elements[i];
						if ((str = element->get<String>())) {
							for (size_t j = 0; j < id_count; ++j) {
								if (ids[j] == *str) {
									if (fn(*this, node, user_arg)) {
										return Status::Success;
									}
									matched = true;
									break;
								}
							}
						}

						if (matched) {
							break;
						}
					}
				}
			}
			else if (status != Status::MethodNotFound) {
				return status;
			}
		}

		for (size_t i = 0; i < node->child_count; ++i) {
			if (!stack.push(node->children[i])) {
				return Status::NoMemory;
			}
		}
	}

	return Status::Success;
}

static bool name_cmp(const char* a, const char* b) {
	return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

NamespaceNode* Context::create_or_find_node(NamespaceNode* start, void* method_frame, StringView name, Context::SearchFlags flags) {
	auto* ptr = name.ptr;
	auto size = name.size;
	if (!size) {
		return nullptr;
	}

	NamespaceNode* node;
	if (*ptr == '\\') {
		node = root;
		++ptr;
		--size;
		if (!size) {
			return node;
		}
	}
	else if (*ptr == '^') {
		node = start;
		while (*ptr == '^') {
			++ptr;
			--size;
			if (!node->parent) {
				return nullptr;
			}
			node = node->parent;
			if (!size) {
				return node;
			}
		}
	}
	else {
		node = start;
	}

	while (true) {
		if (size < 4) {
			return nullptr;
		}

		auto* segment = ptr;
		ptr += 4;
		size -= 4;

	again:
		bool found = false;
		for (size_t i = 0; i < node->child_count; ++i) {
			if (name_cmp(node->children[i]->_name, segment)) {
				node = node->children[i];
				found = true;
				break;
			}
		}

		if (found) {
			if (!size) {
				return node;
			}
			++ptr;
			--size;
		}
		else if (flags == SearchFlags::Search) {
			node = node->parent;
			if (!node) {
				return nullptr;
			}
			goto again;
		}
		else if (flags == SearchFlags::Create) {
			auto* new_node = NamespaceNode::create(segment);
			if (!new_node) {
				return nullptr;
			}
			if (!node->add_child(new_node)) {
				new_node->~NamespaceNode();
				qacpi_os_free(new_node, sizeof(NamespaceNode));
				return nullptr;
			}

			if (method_frame) {
				auto& frame = *static_cast<Interpreter::MethodFrame*>(method_frame);
				new_node->link = frame.node_link;
				frame.node_link = new_node;
			}
			else {
				new_node->link = all_nodes;
				all_nodes = new_node;
			}

			if (!size) {
				return new_node;
			}
			++ptr;
			--size;
			node = new_node;
		}
	}
}

ObjectRef Context::get_package_element(ObjectRef& pkg_obj, uint32_t index) {
	Package* pkg;
	if (!pkg_obj || !(pkg = pkg_obj->get<Package>()) || index >= pkg->data->element_count) {
		return ObjectRef::empty();
	}

	auto& elem = pkg->data->elements[index];

	if (auto unresolved = elem->get<Unresolved>()) {
		NamespaceNode* start;
		if (!pkg_obj->node) {
			start = root;
		}
		else {
			start = pkg_obj->node;
		}

		auto* node = create_or_find_node(start, nullptr, unresolved->name, Context::SearchFlags::Search);
		if (!node) {
			return ObjectRef::empty();
		}
		else if (!node->object) {
			LOG << "qacpi: internal error in Context::get_package_element, node->object is null" << endlog;
			return ObjectRef::empty();
		}

		elem = node->object;
	}

	if (!elem->node) {
		elem->node = pkg_obj->node;
	}
	return pkg->data->elements[index];
}
