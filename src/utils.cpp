#include "qacpi/utils.hpp"
#include "qacpi/os.hpp"

namespace qacpi {
	EisaId EisaId::decode(uint32_t id) {
		static constexpr char CHARS[] = "0123456789ABCDEF";
		id = __builtin_bswap32(id);

		char first = static_cast<char>(0x40 + (id >> 16 & 0b11111));
		char second = static_cast<char>(0x40 + (id >> 21 & 0b11111));
		char third = static_cast<char>(0x40 + (id >> 26 & 0b11111));
		uint8_t revision = id & 0xFF;
		uint8_t product = id >> 8 & 0xFF;

		EisaId str;
		str.id[0] = first;
		str.id[1] = second;
		str.id[2] = third;
		str.id[3] = CHARS[product / 16 % 16];
		str.id[4] = CHARS[product % 16];
		str.id[5] = CHARS[revision / 16 % 16];
		str.id[6] = CHARS[revision % 16];
		return str;
	}

	static uint8_t hex_to_int(char c) {
		return c <= '9' ? (c - '0') : ((c - 'A') + 10);
	}

	uint32_t EisaId::encode() {
		uint32_t value = 0;
		value |= (id[0] - 0x40) << 16;
		value |= ((id[1] - 0x40) << 21);
		value |= ((id[2] - 0x40) << 26);
		value |= (hex_to_int(id[3]) * 16) << 8;
		value |= hex_to_int(id[4]) << 8;
		value |= hex_to_int(id[5]) * 16;
		value |= hex_to_int(id[6]);

		value = __builtin_bswap32(value);
		return value;
	}

	template<typename T>
	static constexpr inline bool is_power_of_two(T value) {
		return !(value & (value - 1));
	}

	template<typename T>
	static constexpr inline bool is_aligned(T value, T align) {
		return !(value & (align - 1));
	}

	static uint8_t get_bit_access_size(const Address& addr) {
		uint8_t bit_access_size;
		if (!addr.reg_bit_offset &&
			is_power_of_two(addr.reg_bit_width) &&
			is_aligned<uint8_t>(addr.reg_bit_width, 8)) {
			bit_access_size = addr.reg_bit_width;
		}
		else if (addr.access_size) {
			bit_access_size = addr.access_size * 8;
		}
		else {
			uint8_t msb = 64 - __builtin_clzll(
				addr.reg_bit_offset +
				addr.reg_bit_width - 1);
			bit_access_size = 1 << msb;

			if (bit_access_size <= 8) {
				bit_access_size = 8;
			}
			else {
				while (!is_aligned<uint64_t>(addr.address, bit_access_size / 8)) {
					bit_access_size /= 2;
				}
			}
		}

		switch (static_cast<qacpi::RegionSpace>(addr.space_id)) {
			case qacpi::RegionSpace::SystemIo:
				bit_access_size = bit_access_size < 32 ? bit_access_size : 32;
				break;
			default:
				bit_access_size = bit_access_size < 64 ? bit_access_size : 64;
				break;
		}

		return bit_access_size;
	}

	static Status validate_addr(const Address& addr, uint8_t& bit_access_size) {
		if (!addr.address) {
			return Status::NotFound;
		}
		else if ((addr.space_id != RegionSpace::SystemIo &&
			addr.space_id != RegionSpace::SystemMemory) ||
			addr.access_size > 4) {
			return Status::Unsupported;
		}

		bit_access_size = get_bit_access_size(addr);

		size_t total_bit_width = (addr.reg_bit_offset + addr.reg_bit_width + bit_access_size - 1) & ~(bit_access_size - 1);
		if (total_bit_width > 64) {
			return Status::Unsupported;
		}

		return Status::Success;
	}

	Status read_from_addr(const Address& addr, uint64_t& res) {
		uint8_t bit_access_size;
		if (auto status = validate_addr(addr, bit_access_size); status != Status::Success) {
			return status;
		}

		uint8_t bit_offset = addr.reg_bit_offset;
		uint8_t bits_remaining = bit_offset + addr.reg_bit_width;

		uint8_t access_size = bit_access_size / 8;

		uint64_t mask = 0xFFFFFFFFFFFFFFFF;
		if (access_size < 8) {
			mask = ~(mask << bit_access_size);
		}

		uint64_t value = 0;

		uint8_t index = 0;
		while (bits_remaining) {
			uint64_t data;
			if (bit_offset >= bit_access_size) {
				data = 0;
				bit_offset -= bit_access_size;
			}
			else {
				uint64_t real_addr = addr.address + index * access_size;

				auto space = static_cast<qacpi::RegionSpace>(addr.space_id);
				Status status;
				if (space == qacpi::RegionSpace::SystemMemory) {
					status = qacpi_os_mmio_read(real_addr, access_size, data);
				}
				else {
					status = qacpi_os_io_read(real_addr, access_size, data);
				}

				if (status != Status::Success) {
					return status;
				}
			}

			value |= (data & mask) << (index * bit_access_size);
			bits_remaining -= bits_remaining < bit_access_size ? bits_remaining : bit_access_size;
			++index;
		}

		res = value;

		return Status::Success;
	}

	Status write_to_addr(const Address& addr, uint64_t value) {
		uint8_t bit_access_size;
		if (auto status = validate_addr(addr, bit_access_size); status != Status::Success) {
			return status;
		}

		uint8_t bit_offset = addr.reg_bit_offset;
		uint8_t bits_remaining = bit_offset + addr.reg_bit_width;

		uint8_t access_size = bit_access_size / 8;

		uint64_t mask = 0xFFFFFFFFFFFFFFFF;
		if (access_size < 8) {
			mask = ~(mask << bit_access_size);
		}

		uint8_t index = 0;
		while (bits_remaining) {
			uint64_t data = (value >> (index * bit_access_size)) & mask;

			if (bit_offset >= bit_access_size) {
				bit_offset -= bit_access_size;
			}
			else {
				uint64_t real_addr = addr.address + index * access_size;

				auto space = static_cast<qacpi::RegionSpace>(addr.space_id);
				Status status;
				if (space == qacpi::RegionSpace::SystemMemory) {
					status = qacpi_os_mmio_write(real_addr, access_size, data);
				}
				else {
					status = qacpi_os_io_write(real_addr, access_size, data);
				}

				if (status != Status::Success) {
					return status;
				}
			}

			bits_remaining -= bits_remaining < bit_access_size ? bits_remaining : bit_access_size;
			++index;
		}

		return Status::Success;
	}

	const char* status_to_str(Status status) {
		switch (status) {
			case Status::Success:
				return "success";
			case Status::UnexpectedEof:
				return "unexpected end of data";
			case Status::InvalidAml:
				return "invalid aml";
			case Status::InvalidArgs:
				return "invalid arguments";
			case Status::NoMemory:
				return "not enough memory";
			case Status::NotFound:
				return "object not found";
			case Status::MethodNotFound:
				return "method not found";
			case Status::TimeOut:
				return "operation timed out";
			case Status::Unsupported:
				return "unsupported operation";
			case Status::InternalError:
				return "internal error";
			case Status::EndOfResources:
				return "end of resources";
			case Status::InvalidResource:
				return "invalid resource";
		}
		return "";
	}
}
