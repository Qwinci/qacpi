#pragma once
#include <stddef.h>
#include "utility.hpp"

namespace qacpi {
	template<size_t Value>
	constexpr size_t max() {
		return Value;
	}
	template<size_t Value1, size_t Value2, size_t... Values>
	constexpr size_t max() {
		return max<(Value1 > Value2) ? Value1 : Value2, Values...>();
	}

	template<typename T, typename... Types>
	struct IsAny {
		static constexpr bool value = (is_same_v<T, Types> || ...);
	};

	template<size_t I, typename T, typename U, typename... Types>
	struct TypeIndexHelper {
		static constexpr size_t value = is_same_v<T, U> ? I : TypeIndexHelper<I + 1, T, Types...>::value;
	};
	template<size_t I, typename T, typename U>
	struct TypeIndexHelper<I, T, U> {
		static constexpr size_t value = I;
	};

	template<typename T, typename... Types>
	struct TypeIndex {
		static constexpr size_t value = TypeIndexHelper<1, T, Types...>::value;
	};

	template<typename... Types>
	class Variant {
	public:
		constexpr Variant() = default;

		template<typename T> requires(IsAny<T, Types...>::value)
		inline Variant(T&& value) { // NOLINT(*-explicit-constructor)
			constexpr size_t ID = TypeIndex<T, Types...>::value;

			construct<T>(&storage, move(value));
			id = ID;
		}

		inline Variant(Variant&& other) noexcept {
			id = other.id;
			(move_from<Types>(other) || ...);
		}

		constexpr Variant(const Variant&) = delete;
		constexpr Variant& operator=(const Variant&) = delete;

		bool clone(Variant& res) const {
			bool success;
			(res.clone_from<Types>(*this, success) || ...);
			return success;
		}

		template<typename T> requires(IsAny<T, Types...>::value)
		inline Variant& operator=(T&& value) {
			constexpr size_t ID = TypeIndex<T, Types...>::value;

			if (id) {
				(destroy<Types>() || ...);
			}

			construct<T>(&storage, move(value));
			id = ID;

			return *this;
		}

		template<typename T> requires(IsAny<T, Types...>::value && !requires(T value, const T& other) {
			{value.clone(other) };
		})
		inline Variant& operator=(const T& value) {
			constexpr size_t ID = TypeIndex<T, Types...>::value;

			if (id) {
				(destroy<Types>() || ...);
			}

			construct<T>(&storage, value);
			id = ID;

			return *this;
		}

		template<typename T> requires(IsAny<T, Types...>::value)
		inline T* get() {
			constexpr size_t ID = TypeIndex<T, Types...>::value;
			if (id == ID) {
				return reinterpret_cast<T*>(&storage);
			}
			else {
				return nullptr;
			}
		}

		template<typename T> requires(IsAny<T, Types...>::value)
		inline const T* get() const {
			constexpr size_t ID = TypeIndex<T, Types...>::value;
			if (id == ID) {
				return reinterpret_cast<const T*>(&storage);
			}
			else {
				return nullptr;
			}
		}

		template<typename T> requires(IsAny<T, Types...>::value)
		inline T& get_unsafe() & {
			return *reinterpret_cast<T*>(&storage);
		}

		template<typename T> requires(IsAny<T, Types...>::value)
		inline T get_unsafe() && {
			return move(*reinterpret_cast<T*>(&storage));
		}

		template<typename Visitor>
		void visit(Visitor&& visitor) {
			(visit_internal<Visitor, Types>(visitor) || ...);
		}

		inline Variant& operator=(Variant&& other) noexcept {
			if (id) {
				(destroy<Types>() || ...);
			}

			id = other.id;
			(move_from<Types>(other) || ...);

			return *this;
		}

		[[nodiscard]] constexpr size_t index() const {
			return id;
		}

		~Variant() {
			if (id) {
				(destroy<Types>() || ...);
			}
		}

	private:
		template<typename Visitor, typename T> requires requires(Visitor& visitor, T& arg) {
			visitor(arg);
		}
		inline bool visit_internal(Visitor& visitor) {
			constexpr size_t ID = TypeIndex<T, Types...>::value;
			if (id == ID) {
				visitor(*reinterpret_cast<T*>(&storage));
				return true;
			}
			else {
				return false;
			}
		}
		template<typename Visitor, typename T>
		inline bool visit_internal(Visitor&) {
			return false;
		}

		template<typename T>
		inline bool destroy() {
			constexpr size_t ID = TypeIndex<T, Types...>::value;
			if (id == ID) {
				reinterpret_cast<T*>(&storage)->~T();
				return true;
			}
			else {
				return false;
			}
		}

		template<typename T>
		inline bool move_from(Variant& other) {
			constexpr size_t ID = TypeIndex<T, Types...>::value;
			if (other.id == ID) {
				construct<T>(&storage, move(*reinterpret_cast<T*>(&other.storage)));
				other.id = 0;
				return true;
			}
			else {
				return false;
			}
		}

		template<typename T>
		inline bool clone_from(const Variant& other, bool& success) {
			constexpr size_t ID = TypeIndex<T, Types...>::value;
			if (other.id == ID) {
				if (id) {
					(destroy<Types>() || ...);
				}
				id = ID;

				auto& value = *reinterpret_cast<const T*>(&other.storage);
				T* ptr;
				if constexpr (requires {
					T {value};
				}) {
					ptr = construct<T>(&storage, value);
				}
				else {
					ptr = construct<T>(&storage);
				}
				if constexpr (requires {
					ptr->clone(value);
				}) {
					success = ptr->clone(value);
				}
				else {
					success = true;
				}

				return true;
			}
			else {
				return false;
			}
		}

		size_t id {};
		alignas(max<alignof(Types)...>()) char storage[max<sizeof(Types)...>()] {};
	};
}
