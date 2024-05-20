#pragma once
#include "variant.hpp"
#include "status.hpp"
#include <stdint.h>

namespace qacpi {
	struct ReservedSmallDescriptor {
		uint8_t bytes[8];
	};

	struct IrqDescriptor {
		uint16_t mask_bits;
		uint8_t info;
	};

	static constexpr uint8_t IRQ_INFO_EDGE_TRIGGERED = 1 << 0;
	static constexpr uint8_t IRQ_INFO_ACTIVE_LOW = 1 << 3;
	static constexpr uint8_t IRQ_INFO_SHARED = 1 << 4;
	static constexpr uint8_t IRQ_INFO_WAKE_CAP = 1 << 5;

	struct DmaDescriptor {
		uint8_t channel_mask;
		uint8_t info;
	};

	static constexpr uint8_t DMA_INFO_SIZ_SHIFT = 0;
	static constexpr uint8_t DMA_INFO_SIZ_MASK = 0b11;
	static constexpr uint8_t DMA_INFO_BM = 1 << 2;
	static constexpr uint8_t DMA_INFO_TYP_SHIFT = 5;
	static constexpr uint8_t DMA_INFO_TYPE_MASK = 0b11;

	struct StartDependentDescriptor {
		uint8_t priority;
	};

	static constexpr uint8_t START_DEPENDENT_COMPAT_PRIORITY_SHIFT = 0;
	static constexpr uint8_t START_DEPENDENT_COMPAT_PRIORITY_MASK = 0b11;
	static constexpr uint8_t START_DEPENDENT_PERF_PRIORITY_SHIFT = 2;
	static constexpr uint8_t START_DEPENDENT_PERF_PRIORITY_MASK = 0b11;

	struct EndDependentDescriptor {};

	struct IoPortDescriptor {
		uint8_t pad;
		uint8_t info;
		uint16_t min_base;
		uint16_t max_base;
		uint8_t base_align;
		uint8_t length;
	};

	struct FixedIoPortDescriptor {
		uint16_t base;
		uint8_t length;
	};

	struct FixedDmaDescriptor {
		uint16_t request_line;
		uint16_t channel;
		uint8_t transfer_width;
	};

	struct VendorSpecificDescriptor {
		uint8_t bytes[8];
	};

	struct ReservedLargeDescriptor {
		const uint8_t* data;
		uint16_t length;
	};

	struct Memory24Descriptor {
		uint8_t pad;
		uint8_t info;
		uint16_t min_base;
		uint16_t max_base;
		uint16_t base_align;
		uint16_t length;
	};

	static constexpr uint8_t MEMORY_INFO_RW = 1 << 0;

	struct LargeVendorSpecificDescriptor {
		uint16_t length;
		const uint8_t* data;
	};

	struct Memory32Descriptor {
		uint8_t pad[3];
		uint8_t info;
		uint32_t min_base;
		uint32_t max_base;
		uint32_t base_align;
		uint32_t length;
	};

	struct FixedMemory32Descriptor {
		uint8_t pad[3];
		uint8_t info;
		uint32_t base;
		uint32_t length;
	};

	struct AddressSpaceDescriptor {

	};

	struct QWordAddressSpaceDescriptor {

	};

	struct DWordAddressSpaceDescriptor {

	};

	struct WordAddressSpaceDescriptor {

	};

	struct ExtendedAddressSpaceDescriptor {

	};

	struct ExtendedIrqDescriptor {
		uint8_t info;
		uint8_t irq_table_length;
		const uint8_t* irq_table;
	};

	static constexpr uint8_t EXT_IRQ_INFO_CONSUMER = 1 << 0;
	static constexpr uint8_t EXT_IRQ_INFO_EDGE_TRIGGERED = 1 << 1;
	static constexpr uint8_t EXT_IRQ_INFO_ACTIVE_LOW = 1 << 2;
	static constexpr uint8_t EXT_IRQ_INFO_SHARED = 1 << 3;
	static constexpr uint8_t EXT_IRQ_INFO_WAKE_CAP = 1 << 4;

	struct GenericRegisterDescriptor {

	};

	struct GpioConnectionDescriptor {

	};

	struct GenericSerialBusConnectionDescriptor {

	};

	struct PinFunctionDescriptor {

	};

	struct PinConfigurationDescriptor {

	};

	struct PinGroupDescriptor {

	};

	struct PinGroupFunctionDescriptor {

	};

	struct PinGroupConfigurationDescriptor {

	};

	using Resource = Variant<
		ReservedSmallDescriptor,
		IrqDescriptor,
		DmaDescriptor,
		StartDependentDescriptor,
		EndDependentDescriptor,
		IoPortDescriptor,
		FixedIoPortDescriptor,
		FixedDmaDescriptor,
		VendorSpecificDescriptor,
		Memory24Descriptor,
		LargeVendorSpecificDescriptor,
		Memory32Descriptor,
		FixedMemory32Descriptor,
		ExtendedIrqDescriptor,
		ReservedLargeDescriptor
	>;

	Status resource_parse(const uint8_t* data, size_t size, size_t& offset, Resource& res);
}
