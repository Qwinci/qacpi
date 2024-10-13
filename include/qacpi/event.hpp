#pragma once
#include <qacpi/status.hpp>
#include <stdint.h>

namespace qacpi {
	struct Context;
	struct NamespaceNode;
}

namespace qacpi::events {
	enum class GpeTrigger {
		Edge,
		Level
	};

	enum class FixedEvent {
		Timer = 0,
		PowerButton = 8,
		SleepButton = 9,
		Rtc = 10
	};

	enum class SleepState {
		S1 = 1,
		S2,
		S3,
		S4,
		S5
	};

	struct Context {
		Status init(const void* fadt_ptr);

		Status enable_events_from_ns(qacpi::Context& ctx);

		Status enable_fixed_event(FixedEvent event, void (*handler)(void* arg), void* arg);
		Status disable_fixed_event(FixedEvent event);

		Status enable_gpe(uint32_t index, GpeTrigger trigger, void (*handler)(void* arg), void* arg);
		Status disable_gpe(uint32_t index);

		Status install_notify_handler(
			qacpi::NamespaceNode* node,
			void (*handler)(void* arg, NamespaceNode* node, uint64_t value),
			void* arg);
		void uninstall_notify_handler(qacpi::NamespaceNode* node);

		void on_notify(qacpi::NamespaceNode* node, uint64_t value);

		Status enable_acpi_mode(bool enable);

		Status prepare_for_sleep_state(qacpi::Context& ctx, SleepState state);
		Status enter_sleep_state(SleepState state);

		Status prepare_for_wake();
		Status wake_from_state(qacpi::Context& ctx, SleepState state);

		~Context();

	private:
		friend bool qacpi_on_sci(void* arg);

		struct Inner;
		Inner* inner {};

		struct NotifyHandler;
		NotifyHandler* notify_handlers {};

		uint8_t slp_typa_s0 {0xFF};
		uint8_t slp_typb_s0 {0xFF};

		uint8_t slp_typa {};
		uint8_t slp_typb {};
	};
}
