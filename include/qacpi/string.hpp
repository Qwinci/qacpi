#pragma once
#include <stddef.h>
#include "shared_ptr.hpp"

namespace qacpi {
	class String {
	public:
		String() = default;

		String(String&& other) noexcept;

		constexpr String(const String&) = delete;
		constexpr String& operator=(const String&) = delete;

		String& operator=(String&& other) noexcept;

		bool init(const char* str, size_t size);
		bool init_with_size(size_t size);

		bool clone(const String& other);

		constexpr char* data() {
			return _data->ptr;
		}

		[[nodiscard]] constexpr const char* data() const {
			return _data->ptr;
		}

		[[nodiscard]] constexpr size_t size() const {
			return _data->size;
		}

		[[nodiscard]] constexpr bool is_path() const {
			return _is_path;
		}

		constexpr void mark_as_path() {
			_is_path = true;
		}

	private:
		struct Data {
			~Data();

			char* ptr {};
			size_t size {};
		};
		SharedPtr<Data> _data {};
		bool _is_path {};
	};
}
