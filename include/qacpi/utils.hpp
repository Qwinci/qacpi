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
		uint8_t device;
		uint8_t function;
	};

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
		Pcc = 0xA,
		TableData = 0xB
	};

	struct [[gnu::packed]] Address {
		RegionSpace space_id;
		uint8_t reg_bit_width;
		uint8_t reg_bit_offset;
		uint8_t access_size;
		uint64_t address;
	};

	Status read_from_addr(const Address& addr, uint64_t& res);
	Status write_to_addr(const Address& addr, uint64_t value);

	const char* status_to_str(Status status);

	static constexpr EisaId PCIE_ID {"PNP0A08"};
	static constexpr EisaId PCI_ID {"PNP0A03"};
}
