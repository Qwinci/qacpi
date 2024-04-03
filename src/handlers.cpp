#include "qacpi/handlers.hpp"
#include "qacpi/ns.hpp"
#include "qacpi/utils.hpp"

namespace qacpi {
	static constexpr EisaId PCIE_ID {"PNP0A08"};
	static constexpr EisaId PCI_ID {"PNP0A03"};

	Status pci_config_attach(Context* ctx, NamespaceNode* node) {
		while (node) {
			auto res = ObjectRef::empty();
			auto status = ctx->evaluate(node, "_HID", res);

			EisaId hid_id;
			EisaId cid_id;

			if (status == Status::Success) {
				if (auto str = res->get<String>()) {
					if (str->size() >= 6) {
						hid_id = EisaId {str->data(), str->size()};
					}
				}
				else if (auto integer = res->get<uint64_t>()) {
					hid_id = EisaId::decode(*integer);
				}
			}
			else if (status != Status::MethodNotFound) {
				return status;
			}

			if (hid_id != PCI_ID && hid_id != PCIE_ID) {
				status = ctx->evaluate(node, "_CID", res);
				if (status == Status::Success) {
					if (auto str = res->get<String>()) {
						if (str->size() >= 6) {
							cid_id = EisaId {str->data(), str->size()};
						}
					}
					else if (auto integer = res->get<uint64_t>()) {
						cid_id = EisaId::decode(*integer);
					}
					else if (auto pkg = res->get<Package>()) {
						for (uint32_t i = 0; i < pkg->data->element_count; ++i) {
							auto& element = pkg->data->elements[i];
							if ((str = element->get<String>())) {
								if (str->size() >= 6) {
									cid_id = EisaId {str->data(), str->size()};
								}
							}
							else if ((integer = element->get<uint64_t>())) {
								cid_id = EisaId::decode(*integer);
							}

							if (cid_id == PCI_ID || cid_id == PCIE_ID) {
								break;
							}
						}
					}
				}
				else if (status != Status::MethodNotFound) {
					return status;
				}
				else {
					node = node->get_parent();
					continue;
				}
			}

			if (hid_id == PCIE_ID || hid_id == PCI_ID || cid_id == PCI_ID || cid_id == PCIE_ID) {
				uint16_t seg;
				status = ctx->evaluate(node, "_SEG", res);
				if (status == Status::MethodNotFound) {
					seg = 0;
				}
				else if (status != Status::Success) {
					return status;
				}
				else {
					seg = res->get_unsafe<uint64_t>() & 0xFFFFFFFF;
				}

				uint8_t bus;
				status = ctx->evaluate(node, "_BBN", res);
				if (status == Status::MethodNotFound) {
					bus = 0;
				}
				else if (status != Status::Success) {
					return status;
				}
				else {
					bus = res->get_unsafe<uint64_t>() & 0xFF;
				}

				uint16_t device;
				uint16_t function;
				status = ctx->evaluate(node, "_ADR", res);
				if (status == Status::MethodNotFound) {
					device = 0;
					function = 0;
				}
				else if (status != Status::Success) {
					return status;
				}
				else {
					device = res->get_unsafe<uint64_t>() >> 16 & 0xFFFF;
					function = res->get_unsafe<uint64_t>() & 0xFFFF;
				}

				auto& region = node->get_object()->get_unsafe<OpRegion>();
				region.pci_address = {
					.segment = seg,
					.bus = bus,
					.device = device,
					.function = function
				};
				return Status::Success;
			}
			else {
				node = node->get_parent();
			}
		}

		return Status::Unsupported;
	}

	Status pci_config_detach(Context*, NamespaceNode*) {
		return Status::Success;
	}

	Status pci_config_read(NamespaceNode* node, uint64_t offset, uint8_t size, uint64_t& res, void*) {
		auto& region = node->get_object()->get_unsafe<OpRegion>();
		return qacpi_os_pci_read(region.pci_address, offset, size, res);
	}

	Status pci_config_write(NamespaceNode* node, uint64_t offset, uint8_t size, uint64_t value, void*) {
		auto& region = node->get_object()->get_unsafe<OpRegion>();
		return qacpi_os_pci_write(region.pci_address, offset, size, value);
	}

	constexpr RegionSpaceHandler PCI_CONFIG_HANDLER {
		.attach = pci_config_attach,
		.detach = pci_config_detach,
		.read = pci_config_read,
		.write = pci_config_write,
		.arg = nullptr,
		.id = RegionSpace::PciConfig
	};
}
