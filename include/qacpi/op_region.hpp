#pragma once
#include <stdint.h>
#include "utils.hpp"
#include "status.hpp"

namespace qacpi {
	enum class RegionSpace : uint8_t {
		SystemMemory = 0x0,
		SystemIo = 0x1,
		PciConfig = 0x2,
		EmbeddedControl = 0x3,
		SmBus = 0x4,
		SystemCmos = 0x5,
		PciBarTarget = 0x6,
		Ipmi = 0x7,
		GeneralPurposeIo = 0x8,
		GenericSerialBus = 0x9,
		Pcc = 0xA
	};

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
		Status run_reg(bool global);
	};
}
