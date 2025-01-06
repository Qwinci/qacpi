#pragma once
#include <stdint.h>
#include "utils.hpp"
#include "status.hpp"

namespace qacpi {
	struct Context;
	struct NamespaceNode;

	struct OpRegion {
		Context* ctx;
		NamespaceNode* node;
		uint64_t offset;
		uint64_t size;
		void* handle;
		size_t* refs;
		PciAddress pci_address;
		RegionSpace space;
		bool attached;
		bool regged;

		constexpr OpRegion() = default;
		OpRegion(OpRegion&& other) noexcept;
		OpRegion& operator=(const OpRegion&) = delete;

		bool init();

		bool clone(const OpRegion& other);

		~OpRegion();

		Status read(uint64_t offset, uint8_t size, uint64_t& res);
		Status write(uint64_t offset, uint8_t size, uint64_t value);
		Status run_reg();
	};
}
