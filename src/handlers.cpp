#include "qacpi/handlers.hpp"
#include "qacpi/ns.hpp"
#include "qacpi/utils.hpp"

namespace qacpi {
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
			else if (status != Status::NotFound) {
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
						for (uint32_t i = 0; i < pkg->size(); ++i) {
							auto element = ctx->get_pkg_element(res, i);
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
				else if (status != Status::NotFound) {
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
				if (status == Status::NotFound) {
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
				if (status == Status::NotFound) {
					bus = 0;
				}
				else if (status != Status::Success) {
					return status;
				}
				else {
					bus = res->get_unsafe<uint64_t>() & 0xFF;
				}

				uint8_t device;
				uint8_t function;
				status = ctx->evaluate(node, "_ADR", res);
				if (status == Status::NotFound) {
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

	constinit RegionSpaceHandler PCI_CONFIG_HANDLER {
		.attach = pci_config_attach,
		.detach = pci_config_detach,
		.read = pci_config_read,
		.write = pci_config_write,
		.arg = nullptr,
		.id = RegionSpace::PciConfig
	};
}
