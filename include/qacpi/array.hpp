#pragma once
#include <stddef.h>

namespace qacpi {
	template<typename T, size_t N>
	struct Array {
		T data[N];

		constexpr T& operator[](size_t index) {
			return data[index];
		}

		constexpr const T& operator[](size_t index) const {
			return data[index];
		}
	};
}
