#include "qacpi/utils.hpp"

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
