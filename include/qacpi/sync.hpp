#pragma once
#include <stdint.h>
#include "status.hpp"

namespace qacpi {
	struct Mutex {
		constexpr Mutex() = default;

		Mutex(Mutex&& other) noexcept;

		constexpr Mutex(const Mutex&) = delete;
		constexpr Mutex& operator=(const Mutex&) = delete;

		~Mutex();

		bool init();

		bool clone(const Mutex& other);

		bool is_owned_by_thread();

		Status lock(uint16_t timeout_ms);
		Status unlock();

		void* handle {};
		void* owner {};
		Mutex* prev {};
		Mutex* next {};
		int recursion {};
		uint8_t sync_level {};
	};

	struct Event {
		constexpr Event() = default;

		Event(Event&& other) noexcept;

		constexpr Event(const Event&) = delete;
		constexpr Event& operator=(const Event&) = delete;

		~Event();

		bool init();

		bool clone(const Event& other);

		Status signal();
		Status reset();
		Status wait(uint16_t timeout_ms);

		void* handle {};
	};
}
