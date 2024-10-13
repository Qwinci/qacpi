#include "qacpi/string.hpp"
#include "qacpi/os.hpp"
#include "internal.hpp"

namespace qacpi {
	String::String(qacpi::String&& other) noexcept {
		_data = move(other._data);
	}

	String& String::operator=(String&& other) noexcept {
		_data = move(other._data);
		return *this;
	}

	String::Data::~Data() {
		if (ptr) {
			qacpi_os_free(ptr, size + 1);
		}
	}

	bool String::init(const char* str, size_t size) {
		if (!_data) {
			return false;
		}

		auto* new_ptr = static_cast<char*>(qacpi_os_malloc(size + 1));
		if (!new_ptr) {
			return false;
		}
		memcpy(new_ptr, str, size);
		new_ptr[size] = 0;
		_data->ptr = new_ptr;
		_data->size = size;
		return true;
	}

	bool String::init_with_size(size_t size) {
		if (!_data) {
			return false;
		}

		auto* new_ptr = static_cast<char*>(qacpi_os_malloc(size + 1));
		if (!new_ptr) {
			return false;
		}
		new_ptr[size] = 0;
		_data->ptr = new_ptr;
		_data->size = size;
		return true;
	}

	bool String::clone(const String& other) {
		return init(other._data->ptr, other._data->size);
	}
}
