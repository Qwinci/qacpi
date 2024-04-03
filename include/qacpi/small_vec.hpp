#pragma once
#include "utility.hpp"
#include "os.hpp"

namespace qacpi {
	template<typename T, size_t N>
	class SmallVec {
	public:
		constexpr SmallVec() = default;

		~SmallVec() {
			if (_size <= N) {
				for (size_t i = 0; i < _size; ++i) {
					data.small[i].~T();
				}
			}
			else {
				for (size_t i = 0; i < N; ++i) {
					data.small[i].~T();
				}
				for (size_t i = 0; i < _size - N; ++i) {
					ptr[i].~T();
				}
			}

			if (ptr) {
				qacpi_os_free(ptr, cap * sizeof(T));
			}
		}

		[[nodiscard]] bool push(T&& value) {
			if (_size < N) {
				construct<T>(&data.small[_size++], move(value));
			}
			else {
				if (_size == cap) {
					size_t new_cap = cap * 2;
					auto new_ptr = static_cast<T*>(qacpi_os_malloc(new_cap * sizeof(T)));
					if (!new_ptr) {
						return false;
					}
					for (size_t i = 0; i < _size - N; ++i) {
						construct<T>(&new_ptr[i], move(ptr[i]));
					}
					if (ptr) {
						qacpi_os_free(ptr, cap * sizeof(T));
					}
					ptr = new_ptr;
					cap = new_cap;
				}

				construct<T>(&ptr[_size++ - N], move(value));
			}
			return true;
		}

		[[nodiscard]] bool push(const T& value) {
			if (_size < N) {
				construct<T>(&data.small[_size++], value);
			}
			else {
				if (_size == cap) {
					size_t new_cap = cap * 2;
					auto new_ptr = static_cast<T*>(qacpi_os_malloc(new_cap * sizeof(T)));
					if (!new_ptr) {
						return false;
					}
					for (size_t i = 0; i < _size - N; ++i) {
						construct<T>(&new_ptr[i], move(ptr[i]));
					}
					if (ptr) {
						qacpi_os_free(ptr, cap * sizeof(T));
					}
					ptr = new_ptr;
					cap = new_cap;
				}

				construct<T>(&ptr[_size++ - N], value);
			}
			return true;
		}

		T pop() {
			if (_size <= N) {
				return move(data.small[--_size]);
			}
			else {
				return move(ptr[--_size - N]);
			}
		}

		void pop_discard() {
			if (_size <= N) {
				data.small[--_size].~T();
			}
			else {
				ptr[--_size - N].~T();
			}
		}

		constexpr T& operator[](size_t index) {
			return index < N ? data.small[index] : ptr[index - N];
		}

		constexpr const T& operator[](size_t index) const {
			return index < N ? data.small[index] : ptr[index - N];
		}

		[[nodiscard]] constexpr size_t size() const {
			return _size;
		}

		[[nodiscard]] constexpr bool is_empty() const {
			return !_size;
		}

		constexpr T& back() {
			return _size <= N ? data.small[_size - 1] : ptr[_size - N - 1];
		}

	private:
		union Data {
			Data() {}
			~Data() {}
			T small[N];
		} data;
		T* ptr {};
		size_t _size {};
		size_t cap {N};
	};
}
