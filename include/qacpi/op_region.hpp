#pragma once
#include <stdint.h>
#include "utils.hpp"
#include "status.hpp"

namespace qacpi {
	struct Context;
	struct NamespaceNode;

	struct OpRegion {
		Context& ctx;
		NamespaceNode* node;
		uint64_t offset;
		uint64_t size;
		PciAddress pci_address;
		RegionSpace space;
		bool attached;
		bool regged;

		Status read(uint64_t offset, uint8_t size, uint64_t& res);
		Status write(uint64_t offset, uint8_t size, uint64_t value);
		Status run_reg();
	};
}
