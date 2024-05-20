#pragma once
#include <stdint.h>
#include <stddef.h>
#include "status.hpp"

namespace qacpi {
	struct EisaId {
		constexpr EisaId() = default;

		consteval explicit EisaId(const char (&new_id)[8]) {
			for (int i = 0; i < 7; ++i) {
				id[i] = new_id[i];
			}
		}

		constexpr explicit EisaId(const char* new_id, size_t len) {
			if (len < 7) {
				return;
			}
			for (int i = 0; i < 7; ++i) {
				id[i] = new_id[i];
			}
		}

		static EisaId decode(uint32_t id);

		uint32_t encode();

		constexpr bool operator==(const EisaId& other) const {
			return id[0] == other.id[0] && id[1] == other.id[1] &&
				id[2] == other.id[2] && id[3] == other.id[3] &&
				id[4] == other.id[4] && id[5] == other.id[5] &&
				id[6] == other.id[6];
		}

		char id[7] {};
	};

	struct PciAddress {
		uint16_t segment;
		uint8_t bus;
		uint16_t device;
		uint16_t function;
	};

	const char* status_to_str(Status status);

	static constexpr EisaId PCIE_ID {"PNP0A08"};
	static constexpr EisaId PCI_ID {"PNP0A03"};
}
