#include "qacpi/op_region.hpp"
#include "qacpi/ns.hpp"
#include "logger.hpp"

namespace qacpi {
	static constexpr uint64_t OP_REGION_DISCONNECT = 0;
	static constexpr uint64_t OP_REGION_CONNECT = 1;

	Status OpRegion::run_reg() {
		if (space != RegionSpace::SystemMemory && space != RegionSpace::SystemIo) {
			bool found = false;

			for (auto handler = ctx.region_handlers; handler; handler = handler->next) {
				if (handler->id == space) {
					found = true;
					break;
				}
			}

			if (!found) {
				return Status::NotFound;
			}
		}

		ObjectRef args[2];
		if (!args[0] || !args[1]) {
			return Status::NoMemory;
		}
		args[0]->data = static_cast<uint64_t>(space);
		args[1]->data = OP_REGION_CONNECT;

		ObjectRef res {ObjectRef::empty()};
		auto status = ctx.evaluate(node->get_parent(), "_REG", res, args, 2);
		if (status == Status::Success) {
			regged = true;
		}
		else {
			return status;
		}
		return Status::Success;
	}

	Status OpRegion::read(uint64_t field_offset, uint8_t field_size, uint64_t& res) {
		switch (space) {
			case RegionSpace::SystemMemory:
			{
				return qacpi_os_mmio_read(offset + field_offset, field_size, res);
			}
			case RegionSpace::SystemIo:
				return qacpi_os_io_read(offset + field_offset, field_size, res);
			default:
			{
				for (auto handler = ctx.region_handlers; handler; handler = handler->next) {
					if (handler->id == space) {
						if (!attached) {
							if (auto status = handler->attach(&ctx, node); status != Status::Success) {
								return status;
							}
							attached = true;
						}

						return handler->read(node, offset + field_offset, field_size, res, handler->arg);
					}
				}
				LOG << "qacpi warning: unhandled read in region " << node->name() << " (space "
					<< static_cast<uint8_t>(space) << ")" << endlog;
				res = 0xFFFFFFFFFFFFFFFF;
				return Status::Success;
			}
		}
	}

	Status OpRegion::write(uint64_t field_offset, uint8_t field_size, uint64_t value) {
		switch (space) {
			case RegionSpace::SystemMemory:
			{
				return qacpi_os_mmio_write(offset + field_offset, field_size, value);
			}
			case RegionSpace::SystemIo:
				return qacpi_os_io_write(offset + field_offset, field_size, value);
			default:
			{
				for (auto handler = ctx.region_handlers; handler; handler = handler->next) {
					if (handler->id == space) {
						if (!attached) {
							if (auto status = handler->attach(&ctx, node); status != Status::Success) {
								return status;
							}
							attached = true;
						}

						return handler->write(node, offset + field_offset, field_size, value, handler->arg);
					}
				}
				LOG << "qacpi warning: unhandled write in region " << node->name() << " (space "
				    << static_cast<uint8_t>(space) << ")" << endlog;
				return Status::Success;
			}
		}
	}
}
