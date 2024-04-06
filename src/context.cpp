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
	auto* mem = qacpi_os_malloc(sizeof(Interpreter));
	if (!mem) {
		return Status::NoMemory;
	}
	auto* interp = construct<Interpreter>(mem, this, static_cast<uint8_t>(revision >= 2 ? 8 : 4));

	auto* node = interp->create_or_get_node(name, Interpreter::SearchFlags::Search);
	if (!node) {
		return Status::MethodNotFound;
	}
	if (!node->object) {
		LOG << "qacpi internal error in Context::evaluate, node->object is null" << endlog;
		return Status::InternalError;
	}
	if (node->object->get<Method>()) {
		auto status = interp->invoke_method(node, res, args, arg_count);

		interp->~Interpreter();
		qacpi_os_free(mem, sizeof(Interpreter));
		return status;
	}
	else {
		res = node->object;

		interp->~Interpreter();
		qacpi_os_free(mem, sizeof(Interpreter));

		return Status::Success;
	}
}

Status Context::evaluate(NamespaceNode* node, StringView name, ObjectRef& res, ObjectRef* args, int arg_count) {
	if (!node) {
		return Status::MethodNotFound;
	}

	auto* mem = qacpi_os_malloc(sizeof(Interpreter));
	if (!mem) {
		return Status::NoMemory;
	}
	auto* interp = construct<Interpreter>(mem, this, static_cast<uint8_t>(revision >= 2 ? 8 : 4));

	node = node->get_child(name);
	if (!node) {
		return Status::MethodNotFound;
	}

	if (!node->object) {
		LOG << "qacpi internal error in Context::evaluate, node->object is null" << endlog;
		return Status::InternalError;
	}
	if (node->object->get<Method>()) {
		auto status = interp->invoke_method(node, res, args, arg_count);
		if (status == Status::Success) {
			if (res) {
				while (auto ref = res->get<Ref>()) {
					if (ref->type == Ref::RefOf) {
						break;
					}
					res = ref->inner;
				}
			}
		}

		interp->~Interpreter();
		qacpi_os_free(mem, sizeof(Interpreter));

		return status;
	}
	else {
		res = node->object;

		interp->~Interpreter();
		qacpi_os_free(mem, sizeof(Interpreter));

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
		region.run_reg(this, reg_region, true);
		reg_region = reg_region->public_link;
		regions_to_reg = reg_region;
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
