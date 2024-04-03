#pragma once
#include "qacpi/context.hpp"

namespace qacpi {
	struct EndLog {};
	inline constexpr EndLog endlog {};

	struct Logger {
		Logger& operator<<(StringView str);
		Logger& operator<<(const String& str);
		Logger& operator<<(uint64_t value);
		Logger& operator<<(EndLog);

	private:
		char buf[256] {};
		char* ptr = buf;
	};

	extern Logger LOG;
}
