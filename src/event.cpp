#include "qacpi/event.hpp"
#include "qacpi/utils.hpp"
#include "qacpi/context.hpp"
#include "qacpi/ns.hpp"
#include "qacpi/small_vec.hpp"

using namespace qacpi;

template<typename T>
using Vector = SmallVec<T, 0>;

namespace {
	struct GpeRegister {
		Address sts;
		Address en;

		[[nodiscard]] Status get_sts(uint8_t& res) const {
			uint64_t value;
			if (auto status = read_from_addr(sts, value); status != Status::Success) {
				return status;
			}
			res = value;
			return Status::Success;
		}

		Status clear_sts(uint8_t bit) {
			return write_to_addr(sts, 1 << bit);
		}

		Status clear_all_sts() {
			return write_to_addr(sts, 0xFF);
		}

		Status enable(uint8_t bit) {
			uint64_t value;
			if (auto status = read_from_addr(en, value); status != Status::Success) {
				return status;
			}
			value |= 1 << bit;
			return write_to_addr(en, value);
		}

		Status disable(uint8_t bit) {
			uint64_t value;
			if (auto status = read_from_addr(en, value); status != Status::Success) {
				return status;
			}
			value &= ~(1 << bit);
			return write_to_addr(en, value);
		}

		Status disable_all() {
			return write_to_addr(en, 0);
		}
	};

	struct GpeEvent {
		GpeRegister* reg;
		uint8_t index;
		events::GpeTrigger trigger;
		void (*fn)(void* arg);
		void* arg;
	};

	struct AmlGpeEvent {
		GpeRegister* reg {};
		qacpi::Context& ctx;
		uint8_t index {};
		events::GpeTrigger trigger {};
		StringView method_name {};
	};

	struct GpeBlock {
		Vector<GpeRegister> regs;
		Vector<GpeEvent> enabled_events;
		Vector<AmlGpeEvent> aml_events;
		uint8_t base;
	};
	
	struct [[gnu::packed]] SdtHeader {
		char signature[4];
		uint32_t length;
		uint8_t revision;
		uint8_t checksum;
		char oem_id[6];
		char oem_table_id[8];
		uint32_t oem_revision;
		char creator_id[4];
		uint32_t creator_revision;
	};
	
	struct [[gnu::packed]] Fadt {
		SdtHeader hdr;
		uint32_t fw_ctrl;
		uint32_t dsdt;
		uint8_t reserved0;
		uint8_t preferred_pm_profile;
		uint16_t sci_int;
		uint32_t smi_cmd;
		uint8_t acpi_enable;
		uint8_t acpi_disable;
		uint8_t s4bios_req;
		uint8_t pstate_cnt;
		uint32_t pm1a_evt_blk;
		uint32_t pm1b_evt_blk;
		uint32_t pm1a_cnt_blk;
		uint32_t pm1b_cnt_blk;
		uint32_t pm2_cnt_blk;
		uint32_t pm_tmr_blk;
		uint32_t gpe0_blk;
		uint32_t gpe1_blk;
		uint8_t pm1_evt_len;
		uint8_t pm1_cnt_len;
		uint8_t pm2_cnt_len;
		uint8_t pm_tmr_len;
		uint8_t gpe0_blk_len;
		uint8_t gpe1_blk_len;
		uint8_t gpe1_base;
		uint8_t cst_cnt;
		uint16_t p_lvl2_lat;
		uint16_t p_lvl3_lat;
		uint16_t flush_size;
		uint16_t flush_stride;
		uint8_t duty_offset;
		uint8_t duty_width;
		uint8_t day_alrm;
		uint8_t mon_alrm;
		uint8_t century;
		uint16_t iapc_boot_arch;
		uint8_t reserved2;
		uint32_t flags;
		Address reset_reg;
		uint8_t reset_value;
		uint16_t arm_boot_arch;
		uint8_t fadt_minor_version;
		uint64_t x_firmware_ctrl;
		uint64_t x_dsdt;
		Address x_pm1a_evt_blk;
		Address x_pm1b_evt_blk;
		Address x_pm1a_cnt_blk;
		Address x_pm1b_cnt_blk;
		Address x_pm2_cnt_blk;
		Address x_pm_tmr_blk;
		Address x_gpe0_blk;
		Address x_gpe1_blk;
		Address sleep_ctrl_reg;
		Address sleep_sts_reg;
		uint64_t hypervisor_vendor;
	};

	Address get_addr_from_fadt(const Fadt* fadt, Address Fadt::* extended, uint32_t Fadt::* legacy, uint8_t byte_width) {
		if (fadt->hdr.length >= sizeof(Fadt) && (fadt->*extended).address) {
			return fadt->*extended;
		}
		else {
			return {
				.space_id = RegionSpace::SystemIo,
				.reg_bit_width = static_cast<uint8_t>(byte_width * 8),
				.reg_bit_offset = 0,
				.access_size = 0,
				.address = fadt->*legacy
			};
		}
	}
}

namespace qacpi::events {
	struct FixedEventHandler {
		void (*fn)(void* arg);
		void* arg;
	};

	static Status acpi_aml_gpe_work(void* arg);
	static Status acpi_gpe_work(void* arg);
	static Status acpi_fixed_work(void* arg);

	struct Context::Inner {
		GpeBlock gpe_blocks[2] {};
		Address pm1a_evt_sts {};
		Address pm1a_evt_en {};
		Address pm1b_evt_sts {};
		Address pm1b_evt_en {};
		void* sci_handle {};
		FixedEventHandler fixed_handlers[static_cast<int>(FixedEvent::Rtc) + 1] {};
		uint32_t sci_irq {};
		bool fixed_power_button_supported {};
		bool fixed_sleep_button_supported {};

		bool get_reg(uint32_t index, GpeRegister*& reg) {
			if (!gpe_blocks[1].regs.is_empty() && index >= gpe_blocks[1].base) {
				index -= gpe_blocks[1].base;
				auto reg_index = index / 8;
				if (reg_index >= gpe_blocks[1].regs.size()) {
					return false;
				}

				reg = &gpe_blocks[1].regs[reg_index];
				return true;
			}
			else {
				auto reg_index = index / 8;
				if (reg_index >= gpe_blocks[0].regs.size()) {
					return false;
				}
				reg = &gpe_blocks[0].regs[reg_index];
				return true;
			}
		}

		Status enable_aml_gpe(qacpi::Context& ctx, uint32_t index, StringView method_name, GpeTrigger trigger) {
			GpeRegister* reg;
			if (!get_reg(index, reg)) {
				return Status::InvalidArgs;
			}

			if (!gpe_blocks[1].regs.is_empty() && index >= gpe_blocks[1].base) {
				if (!gpe_blocks[1].aml_events.push(AmlGpeEvent {
					.reg = reg,
					.ctx = ctx,
					.index = static_cast<uint8_t>(index % 8),
					.trigger = trigger,
					.method_name = method_name
				})) {
					return Status::NoMemory;
				}
			}
			else {
				if (!gpe_blocks[0].aml_events.push(AmlGpeEvent {
					.reg = reg,
					.ctx = ctx,
					.index = static_cast<uint8_t>(index % 8),
					.trigger = trigger,
					.method_name = method_name
				})) {
					return Status::NoMemory;
				}
			}

			if (auto status = reg->enable(index % 8); status != Status::Success) {
				if (!gpe_blocks[1].regs.is_empty() && index >= gpe_blocks[1].base) {
					gpe_blocks[1].aml_events.pop_discard();
				}
				else {
					gpe_blocks[0].aml_events.pop_discard();
				}
				return status;
			}

			return Status::Success;
		}

		Status enable_fixed_event(FixedEvent event, bool enable) {
			uint64_t pm1_evt_en;
			if (auto status = read_from_addr(pm1a_evt_en, pm1_evt_en); status != Status::Success) {
				return status;
			}
			if (pm1b_evt_en.address) {
				uint64_t value;
				if (auto status = read_from_addr(pm1b_evt_en, value); status != Status::Success) {
					return status;
				}
				pm1_evt_en |= value;
			}

			if (enable) {
				pm1_evt_en |= 1 << static_cast<int>(event);
			}
			else {
				pm1_evt_en &= ~(1 << static_cast<int>(event));
			}

			if (auto status = write_to_addr(pm1a_evt_en, pm1_evt_en); status != Status::Success) {
				return status;
			}
			if (pm1b_evt_en.address) {
				return qacpi::write_to_addr(pm1b_evt_en, pm1_evt_en);
			}

			return Status::Success;
		}

		static constexpr FixedEvent ALL_EVENTS[] {
			FixedEvent::Timer,
			FixedEvent::PowerButton,
			FixedEvent::SleepButton,
			FixedEvent::Rtc
		};

		bool check_fixed_events() {
			uint64_t pm1_evt_sts;
			if (read_from_addr(pm1a_evt_sts, pm1_evt_sts) != Status::Success) {
				return false;
			}

			if (pm1b_evt_sts.address) {
				uint64_t value;
				if (read_from_addr(pm1b_evt_sts, value) != Status::Success) {
					return false;
				}
				pm1_evt_sts |= value;
			}

			if (pm1_evt_sts & 0b111 << 8) {
				for (auto event : ALL_EVENTS) {
					if (pm1_evt_sts & 1 << static_cast<int>(event)) {
						auto& fixed_event = fixed_handlers[static_cast<int>(event)];

						if (fixed_event.fn) {
							qacpi_os_queue_work(acpi_fixed_work, &fixed_event);
						}
					}
				}

				uint64_t value = pm1_evt_sts & 0b111 << 8;
				if (write_to_addr(pm1a_evt_sts, value) != Status::Success) {
					return true;
				}
				if (pm1b_evt_sts.address) {
					if (qacpi::write_to_addr(pm1b_evt_sts, value) != Status::Success) {
						return true;
					}
				}

				return true;
			}

			return false;
		}

		bool check_gpe_events() {
			for (auto& block : gpe_blocks) {
				for (auto& event : block.enabled_events) {
					uint8_t sts;
					if (event.reg->get_sts(sts) != Status::Success) {
						continue;
					}
					if (sts & 1 << event.index) {
						if (event.reg->disable(event.index) != Status::Success) {
							continue;
						}

						if (event.trigger == GpeTrigger::Edge) {
							if (event.reg->clear_sts(event.index) != Status::Success) {
								continue;
							}
						}

						qacpi_os_queue_work(acpi_gpe_work, &event);
						return true;
					}
				}

				for (auto& event : block.aml_events) {
					uint8_t sts;
					if (event.reg->get_sts(sts) != Status::Success) {
						continue;
					}
					if (sts & 1 << event.index) {
						if (event.reg->disable(event.index) != Status::Success) {
							continue;
						}

						if (event.trigger == GpeTrigger::Edge) {
							if (event.reg->clear_sts(event.index) != Status::Success) {
								continue;
							}
						}

						qacpi_os_queue_work(acpi_aml_gpe_work, &event);
						return true;
					}
				}
			}

			return false;
		}
	};

	bool qacpi_on_sci(void* arg);

	Status Context::init(const void* fadt_ptr) {
		if (!fadt_ptr) {
			return Status::InvalidArgs;
		}
		auto* fadt = static_cast<const Fadt*>(fadt_ptr);
		if (fadt->hdr.signature[0] != 'F' ||
			fadt->hdr.signature[1] != 'A' ||
			fadt->hdr.signature[2] != 'C' ||
			fadt->hdr.signature[3] != 'P') {
			return Status::InvalidArgs;
		}

		Vector<GpeRegister> gpe0_regs {};
		Vector<GpeRegister> gpe1_regs {};

		uint8_t gpe0_reg_count = fadt->gpe0_blk_len / 2;
		uint8_t gpe1_reg_count = fadt->gpe1_blk_len / 2;
		if (!gpe0_regs.reserve(gpe0_reg_count) || !gpe1_regs.reserve(gpe1_reg_count)) {
			return Status::NoMemory;
		}

		auto* ptr = qacpi_os_malloc(sizeof(Inner));
		if (!ptr) {
			return Status::NoMemory;
		}
		inner = construct<Inner>(ptr);
		inner->gpe_blocks[0].regs = move(gpe0_regs);
		inner->gpe_blocks[1].regs = move(gpe1_regs);

		Address gpe0_addr = get_addr_from_fadt(fadt, &Fadt::x_gpe0_blk, &Fadt::gpe0_blk, 1);
		Address gpe1_addr = get_addr_from_fadt(fadt, &Fadt::x_gpe1_blk, &Fadt::gpe1_blk, 1);

		Address pm1a_evt_addr = get_addr_from_fadt(fadt, &Fadt::x_pm1a_evt_blk, &Fadt::pm1a_evt_blk, fadt->pm1_evt_len);
		Address pm1b_evt_addr = get_addr_from_fadt(fadt, &Fadt::x_pm1b_evt_blk, &Fadt::pm1b_evt_blk, fadt->pm1_evt_len);

		inner->pm1a_evt_sts = pm1a_evt_addr;
		inner->pm1a_evt_sts.reg_bit_width /= 2;
		inner->pm1a_evt_en = pm1a_evt_addr;
		inner->pm1a_evt_en.address += fadt->pm1_evt_len / 2;
		inner->pm1a_evt_en.reg_bit_width /= 2;

		if (pm1b_evt_addr.address) {
			inner->pm1b_evt_sts = pm1b_evt_addr;
			inner->pm1b_evt_sts.reg_bit_width /= 2;
			inner->pm1b_evt_en = pm1b_evt_addr;
			inner->pm1b_evt_en.address += fadt->pm1_evt_len / 2;
			inner->pm1b_evt_en.reg_bit_width /= 2;
		}

		auto create_gpe_regs = [](const Address& addr, GpeBlock& block, uint8_t len) {
			if (!addr.address) {
				return;
			}

			uint8_t num_regs = len / 2;
			for (uint8_t i = 0; i < num_regs; ++i) {
				Address sts {
					.space_id = addr.space_id,
					.reg_bit_width = 8,
					.reg_bit_offset = 0,
					.access_size = 1,
					.address = addr.address + i
				};

				Address en {
					.space_id = addr.space_id,
					.reg_bit_width = 8,
					.reg_bit_offset = 0,
					.access_size = 1,
					.address = addr.address + num_regs + i
				};

				(void) block.regs.push({
					.sts = sts,
					.en = en
				});

				block.regs.back().disable_all();
				block.regs.back().clear_all_sts();
			}
		};

		create_gpe_regs(gpe0_addr, inner->gpe_blocks[0], fadt->gpe0_blk_len);
		create_gpe_regs(gpe1_addr, inner->gpe_blocks[1], fadt->gpe1_blk_len);

		auto status = qacpi_os_install_sci_handler(fadt->sci_int, qacpi_on_sci, this, &inner->sci_handle);
		if (status != Status::Success) {
			inner->~Inner();
			qacpi_os_free(inner, sizeof(Inner));
			inner = nullptr;
			return status;
		}

		inner->sci_irq = fadt->sci_int;
		inner->fixed_power_button_supported = !(fadt->flags & 1 << 4);
		inner->fixed_sleep_button_supported = !(fadt->flags & 1 << 5);

		return Status::Success;
	}

	Context::~Context() {
		if (inner) {
			for (auto& block : inner->gpe_blocks) {
				for (auto& reg : block.regs) {
					reg.disable_all();
				}
			}

			qacpi_os_uninstall_sci_handler(inner->sci_irq, inner->sci_handle);

			inner->~Inner();
			qacpi_os_free(inner, sizeof(Inner));
		}
	}

	Status Context::enable_events_from_ns(qacpi::Context& ctx) {
		auto gpe_node = ctx.find_node(nullptr, "_GPE");
		if (!gpe_node) {
			return Status::InternalError;
		}

		Status status = Status::Success;
		auto iter_status = ctx.iterate_nodes(
			gpe_node,
			[&](qacpi::Context&, NamespaceNode* node) {
				auto name = node->name();
				if (name.size != 4 || name.ptr[0] != '_') {
					return IterDecision::Continue;
				}

				GpeTrigger trigger;
				if (name.ptr[1] == 'E') {
					trigger = GpeTrigger::Edge;
				}
				else if (name.ptr[1] == 'L') {
					trigger = GpeTrigger::Level;
				}
				else {
					return IterDecision::Continue;
				}

				uint32_t index = 0;
				for (int i = 2; i < 4; ++i) {
					auto c = name.ptr[i];
					if (c >= '0' && c <= '9') {
						index *= 16;
						index += c - '0';
					}
					else if (c >= 'A' && c <= 'F') {
						index *= 16;
						index += c - 'A' + 10;
					}
					else {
						return IterDecision::Continue;
					}
				}

				auto new_status = inner->enable_aml_gpe(ctx, index, name, trigger);
				if (new_status != Status::Success) {
					status = new_status;
				}

				return IterDecision::Continue;
			});

		if (iter_status != Status::Success) {
			return iter_status;
		}

		return status;
	}

	Status Context::enable_fixed_event(FixedEvent event, void (*handler)(void* arg), void* arg) {
		if ((event == FixedEvent::PowerButton && !inner->fixed_power_button_supported) ||
			(event == FixedEvent::SleepButton && !inner->fixed_sleep_button_supported)) {
			return Status::Unsupported;
		}

		auto& fixed_handler = inner->fixed_handlers[static_cast<int>(event)];
		if (fixed_handler.fn) {
			return Status::InvalidArgs;
		}

		if (auto status = inner->enable_fixed_event(event, true); status != Status::Success) {
			return status;
		}

		fixed_handler.fn = handler;
		fixed_handler.arg = arg;

		return Status::Success;
	}

	Status Context::disable_fixed_event(FixedEvent event) {
		auto& fixed_handler = inner->fixed_handlers[static_cast<int>(event)];
		if (!fixed_handler.fn) {
			return Status::InvalidArgs;
		}

		if (auto status = inner->enable_fixed_event(event, false); status != Status::Success) {
			return status;
		}

		fixed_handler.fn = nullptr;
		fixed_handler.arg = nullptr;

		return Status::Success;
	}

	Status Context::enable_gpe(uint32_t index, GpeTrigger trigger, void (*handler)(void*), void* arg) {
		GpeRegister* reg;
		if (!inner->get_reg(index, reg)) {
			return Status::InvalidArgs;
		}

		if (!inner->gpe_blocks[1].regs.is_empty() && index >= inner->gpe_blocks[1].base) {
			if (!inner->gpe_blocks[1].enabled_events.push(GpeEvent {
				.reg = reg,
				.index = static_cast<uint8_t>(index % 8),
				.trigger = trigger,
				.fn = handler,
				.arg = arg
			})) {
				return Status::NoMemory;
			}
		}
		else {
			if (!inner->gpe_blocks[0].enabled_events.push(GpeEvent {
				.reg = reg,
				.index = static_cast<uint8_t>(index % 8),
				.trigger = trigger,
				.fn = handler,
				.arg = arg
			})) {
				return Status::NoMemory;
			}
		}

		if (auto status = reg->enable(index % 8); status != Status::Success) {
			if (!inner->gpe_blocks[1].regs.is_empty() && index >= inner->gpe_blocks[1].base) {
				inner->gpe_blocks[1].enabled_events.pop_discard();
			}
			else {
				inner->gpe_blocks[0].enabled_events.pop_discard();
			}
			return status;
		}

		return Status::Success;
	}

	Status Context::disable_gpe(uint32_t index) {
		GpeRegister* reg;
		if (!inner->get_reg(index, reg)) {
			return Status::InvalidArgs;
		}

		if (auto status = reg->disable(index % 8); status != Status::Success) {
			return status;
		}

		if (!inner->gpe_blocks[1].regs.is_empty() && index >= inner->gpe_blocks[1].base) {
			for (size_t i = 0; i < inner->gpe_blocks[1].enabled_events.size(); ++i) {
				auto& event = inner->gpe_blocks[1].enabled_events[i];
				if (event.reg == reg && event.index == index % 8) {
					inner->gpe_blocks[1].enabled_events.remove(i);
					return Status::Success;
				}
			}
		}
		else {
			for (size_t i = 0; i < inner->gpe_blocks[0].enabled_events.size(); ++i) {
				auto& event = inner->gpe_blocks[0].enabled_events[i];
				if (event.reg == reg && event.index == index % 8) {
					inner->gpe_blocks[0].enabled_events.remove(i);
					return Status::Success;
				}
			}
		}

		return Status::InvalidArgs;
	}

	struct Context::NotifyHandler {
		NotifyHandler* prev {};
		NotifyHandler* next {};
		NamespaceNode* node {};
		void (*fn)(void* arg, NamespaceNode* node, uint64_t value) {};
		void* arg {};
	};

	Status Context::install_notify_handler(
		qacpi::NamespaceNode* node,
		void (*handler)(void* arg, NamespaceNode* node, uint64_t value),
		void* arg) {
		auto* ptr = qacpi_os_malloc(sizeof(NotifyHandler));
		if (!ptr) {
			return Status::NoMemory;
		}
		auto* obj = construct<NotifyHandler>(ptr);

		obj->next = notify_handlers;
		obj->node = node;
		obj->fn = handler;
		obj->arg = arg;

		if (notify_handlers) {
			notify_handlers->prev = obj;
		}
		notify_handlers = obj;
		return Status::Success;
	}

	void Context::uninstall_notify_handler(qacpi::NamespaceNode* node) {
		for (auto* ptr = notify_handlers; ptr; ptr = ptr->next) {
			if (ptr->node == node) {
				if (ptr->prev) {
					ptr->prev->next = ptr->next;
				}
				else {
					notify_handlers = ptr->next;
				}

				if (ptr->next) {
					ptr->next->prev = ptr->prev;
				}

				qacpi_os_free(ptr, sizeof(NotifyHandler));
				break;
			}
		}
	}

	void Context::on_notify(qacpi::NamespaceNode* node, uint64_t value) {
		for (auto* ptr = notify_handlers; ptr; ptr = ptr->next) {
			if (ptr->node == node) {
				ptr->fn(ptr->arg, node, value);
				break;
			}
		}
	}

	static Status acpi_aml_gpe_work(void* arg) {
		auto* event = static_cast<AmlGpeEvent*>(arg);

		auto gpe = event->ctx.find_node(nullptr, "_GPE");

		auto res = qacpi::ObjectRef::empty();
		auto status = event->ctx.evaluate(gpe, event->method_name, res);

		if (event->trigger == GpeTrigger::Level) {
			event->reg->clear_sts(event->index);
		}

		event->reg->enable(event->index);
		return status;
	}

	static Status acpi_gpe_work(void* arg) {
		auto* event = static_cast<GpeEvent*>(arg);

		event->fn(event->arg);

		if (event->trigger == GpeTrigger::Level) {
			event->reg->clear_sts(event->index);
		}

		event->reg->enable(event->index);
		return Status::Success;
	}

	static Status acpi_fixed_work(void* arg) {
		auto* event = static_cast<FixedEventHandler*>(arg);
		event->fn(event->arg);
		return Status::Success;
	}

	bool qacpi_on_sci(void* arg) {
		auto* ctx = static_cast<Context*>(arg);
		return ctx->inner->check_fixed_events() || ctx->inner->check_gpe_events();
	}
}

