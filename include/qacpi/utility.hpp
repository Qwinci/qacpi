#pragma once
#include <stddef.h>

namespace qacpi {
	template<typename T>
	struct remove_reference {
		using type = T;
	};
	template<typename T>
	struct remove_reference<T&> {
		using type = T;
	};
	template<typename T>
	struct remove_reference<T&&> {
		using type = T;
	};

	template<typename T>
	using remove_reference_t = typename remove_reference<T>::type;

	template<typename T, typename U>
	struct is_same {
		static constexpr bool value = false;
	};
	template<typename T>
	struct is_same<T, T> {
		static constexpr bool value = true;
	};

	template<typename T, typename U>
	inline constexpr bool is_same_v = is_same<T, U>::value;

	namespace __detail {
		template<typename T, typename U>
		concept same_helper = is_same_v<T, U>;
	}

	template<typename T, typename U>
	concept same_as = __detail::same_helper<T, U> && __detail::same_helper<U, T>;

	template<typename T>
	constexpr T&& forward(remove_reference_t<T>& value) {
		return static_cast<T&&>(value);
	}
	template<typename T>
	constexpr T&& forward(remove_reference_t<T>&& value) {
		return static_cast<T&&>(value);
	}

	template<typename T>
	constexpr remove_reference_t<T>&& move(T&& value) {
		return static_cast<remove_reference_t<T>&&>(value);
	}

	template<typename T, typename... Args>
	constexpr T* construct(void* ptr, Args&&... args) {
		struct Container {
			constexpr void* operator new(size_t, void* ptr) {
				return ptr;
			}

			T value;
		};

		return &(new (ptr) Container {{forward<Args&&>(args)...}})->value;
	}
}
