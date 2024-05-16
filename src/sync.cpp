#include "qacpi/sync.hpp"
#include "qacpi/os.hpp"

namespace qacpi {
	Mutex::Mutex(Mutex&& other) noexcept {
		handle = other.handle;
		sync_level = other.sync_level;
		other.handle = nullptr;
	}

	Mutex::~Mutex() {
		if (handle) {
			qacpi_os_mutex_destroy(handle);
		}
	}

	bool Mutex::init() {
		return qacpi_os_mutex_create(&handle);
	}

	bool Mutex::clone(const Mutex& other) {
		sync_level = other.sync_level;
		return init();
	}

	bool Mutex::is_owned_by_thread() {
		return __atomic_load_n(&owner, __ATOMIC_ACQUIRE) == qacpi_os_get_tid();
	}

	Status Mutex::lock(uint16_t timeout_ms) {
		auto status = qacpi_os_mutex_lock(handle, timeout_ms);
		if (status == Status::Success) {
			owner = qacpi_os_get_tid();
		}
		return status;
	}

	Status Mutex::unlock() {
		if (auto status = qacpi_os_mutex_unlock(handle); status != Status::Success) {
			return status;
		}
		owner = nullptr;
		return Status::Success;
	}

	Event::Event(Event&& other) noexcept {
		handle = other.handle;
		other.handle = nullptr;
	}

	Event::~Event() {
		if (handle) {
			qacpi_os_event_destroy(handle);
		}
	}

	bool Event::init() {
		return qacpi_os_event_create(&handle);
	}

	bool Event::clone(const Event&) {
		return init();
	}

	Status Event::signal() {
		return qacpi_os_event_signal(handle);
	}

	Status Event::reset() {
		return qacpi_os_event_reset(handle);
	}

	Status Event::wait(uint16_t timeout_ms) {
		return qacpi_os_event_wait(handle, timeout_ms);
	}
}
