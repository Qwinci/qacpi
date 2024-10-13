#include "qacpi/resources.hpp"
#include "qacpi/os.hpp"
#include "internal.hpp"

qacpi::Status qacpi::resource_parse(const uint8_t* data, size_t size, size_t& offset, Resource& res) {
	if (offset + 2 > size) {
		return Status::EndOfResources;
	}

	uint8_t tag = data[offset];
	// small resource
	if (!(tag & 1 << 7)) {
		uint8_t length = tag & 0b111;
		if (offset + 1 + length > size) {
			return Status::UnexpectedEof;
		}

		++offset;

		uint8_t type = tag >> 3 & 0b1111;
		switch (type) {
			case 0x4:
			{
				IrqDescriptor desc {};

				if (length == 2) {
					memcpy(&desc.mask_bits, data + offset, 2);
					desc.info = IRQ_INFO_EDGE_TRIGGERED;
				}
				else if (length == 3) {
					memcpy(&desc.mask_bits, data + offset, 2);
					desc.info = data[offset + 2];
				}
				else {
					return Status::InvalidResource;
				}

				res = desc;

				break;
			}
			case 0x5:
			{
				DmaDescriptor desc {};
				if (length == 2) {
					memcpy(&desc, data + offset, 2);
				}
				else {
					return Status::InvalidResource;
				}

				res = desc;

				break;
			}
			case 0x6:
			{
				StartDependentDescriptor desc {};
				if (length == 1) {
					desc.priority = data[offset];
				}
				else if (length != 0) {
					return Status::InvalidResource;
				}

				res = desc;

				break;
			}
			case 0x7:
			{
				if (length != 0) {
					return Status::InvalidResource;
				}
				res = EndDependentDescriptor {};
				break;
			}
			case 0x8:
			{
				IoPortDescriptor desc {};
				if (length != 7) {
					return Status::InvalidResource;
				}
				memcpy(&desc, data + offset - 1, 8);
				res = desc;
				break;
			}
			case 0x9:
			{
				FixedIoPortDescriptor desc {};
				if (length != 3) {
					return Status::InvalidResource;
				}
				memcpy(&desc, data + offset, 3);
				desc.base &= 0b1111111111;
				res = desc;
				break;
			}
			case 0xA:
			{
				FixedDmaDescriptor desc {};
				if (length != 5) {
					return Status::InvalidResource;
				}
				memcpy(&desc, data + offset, 5);
				res = desc;
				break;
			}
			case 0xE:
			{
				VendorSpecificDescriptor desc {};
				memcpy(desc.bytes, data + offset - 1, length + 1);
				res = desc;
				break;
			}
			case 0xF:
			{
				++offset;
				return Status::EndOfResources;
			}
			default:
			{
				ReservedSmallDescriptor desc {};
				memcpy(desc.bytes, data + offset - 1, length + 1);
				res = desc;
				break;
			}
		}

		offset += length;
	}
	else {
		if (offset + 3 > size) {
			return Status::UnexpectedEof;
		}

		++offset;
		uint16_t length;
		memcpy(&length, data + offset, 2);
		offset += 2;

		if (offset + length > size) {
			return Status::UnexpectedEof;
		}

		uint8_t type = tag & ~(1 << 7);

		switch (type) {
			case 0x1:
			{
				Memory24Descriptor desc {};
				if (length != 9) {
					return Status::InvalidResource;
				}
				memcpy(&desc, data + offset - 1, 10);
				res = desc;
				break;
			}
			case 0x4:
			{
				LargeVendorSpecificDescriptor desc {
					.length = length,
					.data = data + offset
				};
				res = desc;
				break;
			}
			case 0x5:
			{
				Memory32Descriptor desc {};
				if (length != 17) {
					return Status::InvalidResource;
				}
				memcpy(&desc, data + offset - 3, 20);
				res = desc;
				break;
			}
			case 0x6:
			{
				FixedMemory32Descriptor desc {};
				if (length != 9) {
					return Status::InvalidResource;
				}
				memcpy(&desc, data + offset - 3, 12);
				res = desc;
				break;
			}
			case 0x9:
			{
				if (length < 6) {
					return Status::InvalidResource;
				}

				ExtendedIrqDescriptor desc {
					.info = data[offset],
					.irq_table_length = data[offset + 1],
					.irq_table = data + offset + 2
				};
				res = desc;
				break;
			}
			default:
			{
				ReservedLargeDescriptor desc {
					.data = data + offset,
					.length = length
				};
				res = desc;
				break;
			}
		}

		offset += length;
	}

	return Status::Success;
}
