#pragma once
#include "utility.hpp"
#include "os.hpp"

namespace qacpi {
	template<typename T, size_t N>
	class SmallVec {
	public:
		struct Iterator {
			SmallVec& owner;
			size_t index;

			constexpr T& operator*() {
				return owner[index];
			}

			constexpr void operator++() {
				++index;
			}

			constexpr bool operator!=(const Iterator& other) {
				return index != other.index;
			}
		};

		Iterator begin() {
			return {*this, 0};
		}

		Iterator end() {
			return {*this, _size};
		}

		constexpr SmallVec() = default;
		constexpr SmallVec(const SmallVec&) = delete;
		constexpr SmallVec& operator=(const SmallVec&) = delete;

		constexpr SmallVec(SmallVec&& other) {
			_size = other._size;
			cap = other.cap;
			ptr = other.ptr;

			other._size = 0;
			other.cap = 0;
			other.ptr = nullptr;

			if (_size > N) {
				for (size_t i = 0; i < N; ++i) {
					construct<T>(&data.small[i], move(other.data.small[i]));
					other.data.small[i].~T();
				}
			}
			else {
				for (size_t i = 0; i < _size; ++i) {
					construct<T>(&data.small[i], move(other.data.small[i]));
					other.data.small[i].~T();
				}
			}
		}

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

		constexpr SmallVec& operator=(SmallVec&& other) {
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

			_size = other._size;
			cap = other.cap;
			ptr = other.ptr;

			other._size = 0;
			other.cap = 0;
			other.ptr = nullptr;

			if (_size > N) {
				for (size_t i = 0; i < N; ++i) {
					construct<T>(&data.small[i], move(other.data.small[i]));
					other.data.small[i].~T();
				}
			}
			else {
				for (size_t i = 0; i < _size; ++i) {
					construct<T>(&data.small[i], move(other.data.small[i]));
					other.data.small[i].~T();
				}
			}

			return *this;
		}

		[[nodiscard]] bool push(T&& value) {
			if (_size < N) {
				construct<T>(&data.small[_size++], move(value));
			}
			else {
				if (!reserve(1)) {
					return false;
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
				if (!reserve(1)) {
					return false;
				}

				construct<T>(&ptr[_size++ - N], value);
			}
			return true;
		}

		[[nodiscard]] T* push() {
			if (_size < N) {
				return construct<T>(&data.small[_size++]);
			}
			else {
				if (!reserve(1)) {
					return nullptr;
				}

				return construct<T>(&ptr[_size++ - N]);
			}
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

		void remove(size_t index) {
			if (index >= _size) {
				return;
			}

			(*this)[index].~T();
			for (size_t i = index + 1; i < _size; ++i) {
				(*this)[i - 1] = move((*this)[i]);
			}
			_size -= 1;
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

		bool reserve(size_t amount) {
			if (_size + amount > N + cap) {
				size_t new_cap = cap * 2;
				if (_size + amount > new_cap) {
					new_cap = _size + amount;
				}
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

			return true;
		}

	private:
		union Data {
			Data() {}
			~Data() {}
			T small[N];
		} data;
		T* ptr {};
		size_t _size {};
		size_t cap {};
	};
}
