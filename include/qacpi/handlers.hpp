#pragma once
#include "qacpi/status.hpp"
#include "qacpi/object.hpp"
#include <stdint.h>

namespace qacpi {
	struct Context;

	struct RegionSpaceHandler {
		Status (*attach)(Context* ctx, NamespaceNode* region) {};
		Status (*detach)(Context* ctx, NamespaceNode* region) {};
		Status (*read)(NamespaceNode* region, uint64_t offset, uint8_t size, uint64_t& res, void* arg) {};
		Status (*write)(NamespaceNode* region, uint64_t offset, uint8_t size, uint64_t value, void* arg) {};
		void* arg {};
		const RegionSpaceHandler* prev {};
		const RegionSpaceHandler* next {};
		RegionSpace id {};
	};

	extern const RegionSpaceHandler PCI_CONFIG_HANDLER;
}
