#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <unordered_map>
#include "qacpi/context.hpp"
#include "qacpi/ns.hpp"
#include "qacpi/utils.hpp"

bool qacpi_os_mutex_create(void** handle) {
	*handle = new std::timed_mutex {};
	return true;
}

void qacpi_os_mutex_destroy(void* handle) {
	delete static_cast<std::timed_mutex*>(handle);
}

qacpi::Status qacpi_os_mutex_lock(void* handle, uint16_t timeout_ms) {
	auto* mutex = static_cast<std::timed_mutex*>(handle);
	if (!mutex->try_lock_for(std::chrono::milliseconds {timeout_ms})) {
		return qacpi::Status::TimeOut;
	}
	return qacpi::Status::Success;
}

qacpi::Status qacpi_os_mutex_unlock(void* handle) {
	auto* mutex = static_cast<std::timed_mutex*>(handle);
	mutex->unlock();
	return qacpi::Status::Success;
}

struct OsEvent {
	std::atomic<size_t> counter {};
	std::mutex mutex {};
	std::condition_variable cond {};
};

bool qacpi_os_event_create(void** handle) {
	*handle = new OsEvent {};
	return true;
}

void qacpi_os_event_destroy(void* handle) {
	delete static_cast<OsEvent*>(handle);
}

qacpi::Status qacpi_os_event_wait(void* handle, uint16_t timeout_ms) {
	auto* event = static_cast<OsEvent*>(handle);
	while (true) {
		auto old = event->counter.load(std::memory_order::acquire);
		if (!old) {
			std::unique_lock lock {event->mutex};
			if (!event->cond.wait_for(
				lock,
				std::chrono::milliseconds {timeout_ms},
				[&] {
					auto val = event->counter.load(std::memory_order::acquire);
					if (!val) {
						return false;
					}
					return event->counter.compare_exchange_strong(val, val - 1, std::memory_order::acquire);
				})) {
				return qacpi::Status::TimeOut;
			}
			return qacpi::Status::Success;
		}
		if (event->counter.compare_exchange_strong(old, old - 1, std::memory_order::acquire)) {
			break;
		}
	}
	return qacpi::Status::Success;
}

qacpi::Status qacpi_os_event_signal(void* handle) {
	auto* event = static_cast<OsEvent*>(handle);
	{
		std::unique_lock guard {event->mutex};
		event->counter.fetch_add(1, std::memory_order::release);
	}
	event->cond.notify_one();
	return qacpi::Status::Success;
}

qacpi::Status qacpi_os_event_reset(void* handle) {
	auto* event = static_cast<OsEvent*>(handle);
	event->counter.store(0, std::memory_order::release);
	return qacpi::Status::Success;
}

void* qacpi_os_get_tid() {
	return reinterpret_cast<void*>(pthread_self());
}

void qacpi_os_trace(const char* str, size_t size) {
	std::cerr << std::string_view {str, size} << '\n';
}

std::unordered_map<void*, size_t> ALLOCATIONS;

void* qacpi_os_malloc(size_t size) {
	auto ptr = malloc(size);
	if (!ptr) {
		return nullptr;
	}
	ALLOCATIONS[ptr] = size;
	return ptr;
}

void qacpi_os_free(void* ptr, size_t size) {
	auto iter = ALLOCATIONS.find(ptr);
	if (iter == ALLOCATIONS.end() || iter->second != size) {
		std::cerr << "invalid free\n";
		abort();
	}

	ALLOCATIONS.erase(ptr);
	free(ptr);
}

void qacpi_os_stall(uint64_t us) {
	std::this_thread::sleep_for(std::chrono::microseconds {us});
}

void qacpi_os_sleep(uint64_t ms) {
	std::this_thread::sleep_for(std::chrono::milliseconds {ms});
}

void qacpi_os_fatal(uint8_t type, uint16_t code, uint64_t arg) {
	std::cerr << "qacpi: fatal acpi error type "
		<< static_cast<int>(type) << " code " << code
		<< " arg " << arg << '\n';
}

uint64_t qacpi_os_timer() {
	auto value = std::chrono::high_resolution_clock::now();
	auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(value.time_since_epoch());
	return ns.count() / 100;
}

void qacpi_os_breakpoint() {

}

qacpi::Status qacpi_os_mmio_read(uint64_t phys, uint8_t size, uint64_t& res) {
	std::cerr << "qacpi_os_mmio_read" << static_cast<int>(size) << ' ' << reinterpret_cast<void*>(phys) << '\n';
	res = 0;
	return qacpi::Status::Success;
}

qacpi::Status qacpi_os_mmio_write(uint64_t phys, uint8_t size, uint64_t value) {
	std::cerr << "qacpi_os_mmio_write" << static_cast<int>(size) << ' ' << reinterpret_cast<void*>(phys)
		<< " = " << reinterpret_cast<void*>(value) << '\n';
	return qacpi::Status::Success;
}

qacpi::Status qacpi_os_io_read(uint32_t port, uint8_t size, uint64_t& res) {
	std::cerr << "qacpi_os_io_read" << static_cast<int>(size) << ' ' << reinterpret_cast<void*>(port) << '\n';
	res = 0;
	return qacpi::Status::Success;
}

qacpi::Status qacpi_os_io_write(uint32_t port, uint8_t size, uint64_t value) {
	std::cerr << "qacpi_os_io_write" << static_cast<int>(size) << ' ' << reinterpret_cast<void*>(port)
	          << " = " << reinterpret_cast<void*>(value) << '\n';
	return qacpi::Status::Success;
}

qacpi::Status qacpi_os_pci_read(qacpi::PciAddress address, uint64_t offset, uint8_t size, uint64_t& res) {
	std::cerr << "qacpi_os_pci_read" << static_cast<int>(size) << ' '
	<< reinterpret_cast<void*>(address.segment) << ':' << reinterpret_cast<void*>(address.bus)
	<< ':' << reinterpret_cast<void*>(address.device) << ':' << reinterpret_cast<void*>(address.function)
	<< ' ' << reinterpret_cast<void*>(offset) << '\n';
	res = 0;
	return qacpi::Status::Success;
}

qacpi::Status qacpi_os_pci_write(qacpi::PciAddress address, uint64_t offset, uint8_t size, uint64_t value) {
	std::cerr << "qacpi_os_pci_write" << static_cast<int>(size) << ' '
	          << reinterpret_cast<void*>(address.segment) << ':' << reinterpret_cast<void*>(address.bus)
	          << ':' << reinterpret_cast<void*>(address.device) << ':' << reinterpret_cast<void*>(address.function)
	          << ' ' << reinterpret_cast<void*>(offset) << " = " << reinterpret_cast<void*>(value) << '\n';
	return qacpi::Status::Success;
}

void qacpi_os_notify(void*, qacpi::NamespaceNode* node, uint64_t value) {
	auto path = node->absolute_path();
	std::cerr << "qacpi_os_notify " << path.data() << ' ' << value << '\n';
}

struct SdtHeader {
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

int main() {
	//const char* name = "tests/types.aml";
	const char* name = "../amls/dsdt.dat";
	auto size = std::filesystem::file_size(name);
	std::ifstream file {name, std::ios::binary};
	std::vector<uint8_t> data(size);
	file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
	file.close();

	auto* hdr = reinterpret_cast<const SdtHeader*>(data.data());
	auto* aml = data.data() + sizeof(SdtHeader);
	uint32_t aml_size = hdr->length - sizeof(SdtHeader);

	qacpi::Context ctx {hdr->revision, qacpi::LogLevel::Verbose};
	if (auto status = ctx.init(); status != qacpi::Status::Success) {
		return 1;
	}
	auto status = ctx.load_table(aml, aml_size);
	std::cerr << static_cast<int>(status) << '\n';

	std::vector<uint8_t*> ssdt_datas;

	if (status == qacpi::Status::Success && std::string_view {name} == "../amls/dsdt.dat") {
		for (int i = 0;; ++i) {
			auto ssdt_name = "../amls/ssdt" + (i == 0 ? "" : std::to_string(i)) + ".dat";
			std::ifstream ssdt_file {ssdt_name, std::ios::binary};
			if (!ssdt_file.is_open()) {
				break;
			}
			auto ssdt_size = std::filesystem::file_size(ssdt_name);
			auto* ssdt_data = new uint8_t[ssdt_size];
			ssdt_file.read(reinterpret_cast<char*>(ssdt_data), static_cast<std::streamsize>(ssdt_size));
			ssdt_file.close();

			auto* ssdt_hdr = reinterpret_cast<const SdtHeader*>(ssdt_data);
			auto* ssdt_aml = ssdt_data + sizeof(SdtHeader);
			uint32_t ssdt_aml_size = ssdt_hdr->length - sizeof(SdtHeader);

			status = ctx.load_table(ssdt_aml, ssdt_aml_size);
			std::cerr << "ssdt " << ssdt_name << " status: " << static_cast<int>(status) << '\n';

			ssdt_datas.push_back(ssdt_data);
		}
	}

	status = ctx.init_namespace();
	if (status != qacpi::Status::Success) {
		abort();
	}

	std::cerr << "executing PTS(5)\n";
	qacpi::ObjectRef arg;
	arg->data = uint64_t {5};
	auto res = qacpi::ObjectRef::empty();
	status = ctx.evaluate("_PTS", res, &arg, 1);

	for (auto ssdt_data : ssdt_datas) {
		delete[] ssdt_data;
	}

	return 0;
}
