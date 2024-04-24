#pragma once
#include <stdint.h>
#include "shared_ptr.hpp"
#include "variant.hpp"
#include "string.hpp"
#include "sync.hpp"
#include "utils.hpp"
#include "op_region.hpp"

namespace qacpi {
	struct Uninitialized {};
	struct Debug {};
	struct Device {};

	struct Method {
		const uint8_t* aml;
		Mutex mutex;
		uint32_t size;
		uint8_t arg_count;
		bool serialized;

		bool clone(const Method& other) {
			aml = other.aml;
			size = other.size;
			arg_count = other.arg_count;
			serialized = other.serialized;
			if (serialized && !mutex.init()) {
				return false;
			}
			return true;
		}

	};

	struct Unresolved {
		String name;

		bool clone(const Unresolved& other) {
			return name.clone(other.name);
		}
	};

	struct Buffer {
		constexpr Buffer() = default;

		Buffer(Buffer&& other) noexcept;

		constexpr Buffer(const Buffer&) = delete;
		constexpr Buffer& operator=(const Buffer&) = delete;

		bool init(const void* new_data, uint32_t new_size);
		bool init_with_size(uint32_t new_size);

		bool clone(const Buffer& other);

		struct Data {
			~Data();

			uint8_t* data {};
			uint32_t size {};
		};

		SharedPtr<Data> data {};
	};

	enum class ObjectType {
		Uninitialized = 1,
		Integer,
		String,
		Buffer,
		Package,
		Field,
		Device,
		Event,
		Method,
		Mutex,
		OpRegion,
		PowerRes,
		Processor,
		ThermalZone,
		BufferField,
		Reserved1,
		Debug,
		Ref,
		Arg,
		Unresolved
	};

	struct PowerResource {
		uint16_t resource_order;
		uint8_t system_level;
	};

	struct Processor {
		uint32_t processor_block_addr;
		uint8_t processor_block_size;
		uint8_t id;
	};

	struct ThermalZone {};

	struct Object;

	using ObjectRef = SharedPtr<Object>;

	struct Ref {
		enum {
			RefOf,
			Arg,
			Local
		} type;
		ObjectRef inner;
	};

	struct Package {
		Package() = default;

		Package(Package&& other) noexcept;

		constexpr Package(const Package&) = delete;
		constexpr Package& operator=(const Package&) = delete;

		bool init(uint32_t new_size);

		bool clone(const Package& other);

		struct Data {
			~Data();

			ObjectRef* elements;
			uint32_t element_count;
		};

		SharedPtr<Data> data {};
	};

	enum class FieldUpdate : uint8_t {
		Preserve = 0,
		WriteAsOnes = 1,
		WriteAsZeros = 2
	};

	struct Field {
		enum {
			Normal,
			Index,
			Bank
		} type;

		ObjectRef owner_index;
		ObjectRef data_bank;
		uint64_t bank_value {};
		ObjectRef connection;
		uint32_t bit_size;
		uint32_t bit_offset;
		uint8_t access_size {};
		FieldUpdate update {};
		bool lock {};
	};

	struct BufferField {
		ObjectRef owner;
		uint32_t byte_offset;
		uint32_t byte_size;
		uint32_t total_bit_size;
		uint8_t bit_offset;
		uint8_t bit_size;
	};

	struct NamespaceNode;

	struct NullTarget {};

	struct Object {
		template<typename T>
		inline T* get() {
			return data.get<T>();
		}

		template<typename T>
		inline T& get_unsafe() {
			return data.get_unsafe<T>();
		}

		template<typename T>
		inline const T& get_unsafe() const {
			return data.get_unsafe<T>();
		}

		[[nodiscard]] inline bool is_device() {
			return data.get<Device>() || data.get<Processor>();
		}

		Variant<
			Uninitialized, uint64_t, String, Buffer,
			Package, Field, Device, Event,
			Method, Mutex, OpRegion, PowerResource,
			Processor, ThermalZone, BufferField,
			Unresolved, Debug, Ref, NullTarget> data;
		NamespaceNode* node {};
	};
}
