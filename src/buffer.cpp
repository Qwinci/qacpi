#include "qacpi/object.hpp"
#include "internal.hpp"

namespace qacpi {
	Buffer::Buffer(Buffer&& other) noexcept {
		_data = move(other._data);
	}

	Buffer& Buffer::operator=(Buffer&& other) noexcept {
		_data = move(other._data);
		return *this;
	}

	bool Buffer::init(const void* new_data, uint32_t new_size) {
		if (!_data) {
			return false;
		}

		if (new_size) {
			auto* ptr = static_cast<uint8_t*>(qacpi_os_malloc(new_size));
			if (!ptr) {
				return false;
			}
			memcpy(ptr, new_data, new_size);
			_data->data = ptr;
			_data->size = new_size;
		}
		return true;
	}

	bool Buffer::init_with_size(uint32_t new_size) {
		if (!_data) {
			return false;
		}

		if (new_size) {
			auto* ptr = static_cast<uint8_t*>(qacpi_os_malloc(new_size));
			if (!ptr) {
				return false;
			}
			memset(ptr, 0, new_size);
			_data->data = ptr;
			_data->size = new_size;
		}
		return true;
	}

	bool Buffer::clone(const Buffer& other) {
		return init(other._data->data, other._data->size);
	}

	Buffer::Data::~Data() {
		if (data) {
			qacpi_os_free(data, size);
		}
	}

	Package::Package(Package&& other) noexcept {
		data = move(other.data);
	}

	bool Package::init(uint32_t new_size) {
		if (!data) {
			return false;
		}

		if (new_size) {
			auto* ptr = static_cast<ObjectRef*>(qacpi_os_malloc(new_size * sizeof(ObjectRef)));
			if (!ptr) {
				return false;
			}
			for (uint32_t i = 0; i < new_size; ++i) {
				construct<ObjectRef>(&ptr[i], ObjectRef::empty());
			}
			data->elements = ptr;
			data->element_count = new_size;
		}
		return true;
	}

	bool Package::clone(const Package& other) {
		if (!data) {
			return false;
		}

		if (other.data->element_count) {
			auto* ptr = static_cast<ObjectRef*>(qacpi_os_malloc(other.data->element_count * sizeof(ObjectRef)));
			if (!ptr) {
				return false;
			}
			for (uint32_t i = 0; i < other.data->element_count; ++i) {
				construct<ObjectRef>(&ptr[i], ObjectRef {});
				if (!ptr[i] || !other.data->elements[i]->data.clone(ptr[i]->data)) {
					for (uint32_t j = 0; j <= i; ++j) {
						ptr[j].~SharedPtr();
					}
					qacpi_os_free(ptr, other.data->element_count * sizeof(ObjectRef));
					return false;
				}
				ptr[i]->node = other.data->elements[i]->node;
			}
			data->elements = ptr;
			data->element_count = other.data->element_count;
		}
		return true;
	}

	Package::Data::~Data() {
		if (elements) {
			for (uint32_t i = 0; i < element_count; ++i) {
				elements[i].~SharedPtr();
			}
			qacpi_os_free(elements, element_count * sizeof(ObjectRef));
		}
	}
}
