#include "qacpi/op_region.hpp"
#include "qacpi/ns.hpp"

namespace qacpi {
	static constexpr uint64_t OP_REGION_DISCONNECT = 0;
	static constexpr uint64_t OP_REGION_CONNECT = 1;

	Status OpRegion::run_reg(qacpi::Context* ctx, qacpi::NamespaceNode* node, bool global) {
		ObjectRef args[2];
		if (!args[0] || !args[1]) {
			return Status::NoMemory;
		}
		args[0]->data = static_cast<uint64_t>(space);
		args[1]->data = OP_REGION_CONNECT;

		ObjectRef res {ObjectRef::empty()};
		auto status = ctx->evaluate(node->get_parent(), "_REG", res, args, 2);
		if (status == Status::Success) {
			regged = true;
		}
		else if (status == Status::MethodNotFound) {
			if (global) {
				if (ctx->regions_to_reg == node) {
					return Status::Success;
				}
				else {
					node->public_link = ctx->regions_to_reg;
					ctx->regions_to_reg = node;
				}
			}
		}
		else {
			return status;
		}
		return Status::Success;
	}
}
