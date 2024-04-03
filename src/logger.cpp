#include "logger.hpp"
#include "qacpi/os.hpp"

static constexpr const char* CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXZ";

namespace qacpi {
	Logger& Logger::operator<<(StringView str) {
		if (str.size > 255) {
			qacpi_os_trace("qacpi: debug output is too large", sizeof("qacpi: debug output is too large") - 1);
			return *this;
		}
		if (ptr + str.size > buf + 255) {
			qacpi_os_trace(buf, ptr - buf);
			ptr = buf;
		}
		memcpy(ptr, str.ptr, str.size);
		ptr += str.size;
		return *this;
	}

	Logger& Logger::operator<<(const String& str) {
		operator<<(StringView {str.data(), str.size()});
		return *this;
	}

	Logger& Logger::operator<<(uint64_t value) {
		operator<<("0x");
		char int_buf[16];
		char* int_ptr = int_buf + 16;
		do {
			*--int_ptr = CHARS[value % 16];
			value /= 16;
		} while (value);
		return operator<<(StringView {int_ptr, static_cast<size_t>((int_buf + 16) - int_ptr)});
	}

	Logger& Logger::operator<<(EndLog) {
		*ptr++ = 0;
		qacpi_os_trace(buf, ptr - buf - 1);
		ptr = buf;
		return *this;
	}

	Logger LOG {};
}
