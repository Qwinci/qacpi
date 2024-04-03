#pragma once
#include "utility.hpp"
#include "os.hpp"

namespace qacpi {
	template<typename T>
	class SharedPtr {
	public:
		static constexpr SharedPtr empty() {
			return SharedPtr {Empty {}};
		}

		template<typename... Args>
		explicit SharedPtr(Args&&... args) {
			auto* storage = static_cast<char*>(qacpi_os_malloc(sizeof(T) + sizeof(size_t)));
			if (storage) {
				ptr = construct<T>(storage, forward<Args&&>(args)...);
				refs = construct<size_t>(storage + sizeof(T), size_t {1});
			}
			else {
				ptr = nullptr;
				refs = nullptr;
			}
		}

		constexpr SharedPtr(SharedPtr&& other) noexcept {
			ptr = other.ptr;
			refs = other.refs;
			other.ptr = nullptr;
			other.refs = nullptr;
		}

		constexpr SharedPtr(const SharedPtr& other) {
			ptr = other.ptr;
			refs = other.refs;
			if (refs) {
				++*refs;
			}
		}

		constexpr ~SharedPtr() {
			if (refs) {
				if (--*refs == 0) {
					ptr->~T();
					qacpi_os_free(ptr, sizeof(T) + sizeof(size_t));
				}
			}
		}

		constexpr SharedPtr& operator=(SharedPtr&& other) noexcept {
			if (refs) {
				if (--*refs == 0) {
					ptr->~T();
					qacpi_os_free(ptr, sizeof(T) + sizeof(size_t));
				}
			}
			ptr = other.ptr;
			refs = other.refs;
			other.ptr = nullptr;
			other.refs = nullptr;
			return *this;
		}

		constexpr SharedPtr& operator=(const SharedPtr& other) {
			if (&other == this) {
				return *this;
			}

			if (refs) {
				if (--*refs == 0) {
					ptr->~T();
					qacpi_os_free(ptr, sizeof(T) + sizeof(size_t));
				}
			}
			ptr = other.ptr;
			refs = other.refs;
			if (refs) {
				++*refs;
			}

			return *this;
		}

		constexpr T* operator->() {
			return ptr;
		}

		constexpr const T* operator->() const {
			return ptr;
		}

		constexpr T& operator*() {
			return *ptr;
		}

		constexpr const T& operator*() const {
			return *ptr;
		}

		[[nodiscard]] constexpr size_t ref_count() const {
			return *refs;
		}

		constexpr explicit operator bool() const {
			return ptr;
		}

	private:
		struct Empty {};
		constexpr explicit SharedPtr(Empty) : ptr {nullptr}, refs {nullptr} {}

		T* ptr;
		size_t* refs;
	};
}
