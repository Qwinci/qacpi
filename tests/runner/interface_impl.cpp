#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <unordered_set>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach_time.h>
#include <unistd.h>
#include <fcntl.h>
#else
#include <ctime>
#include <fcntl.h>
#endif

#include "qacpi/os.hpp"
#include <unordered_map>

std::unordered_map<uint64_t, uint8_t> memory;

qacpi::Status qacpi_os_mmio_read(uint64_t addr, uint8_t size, uint64_t& res) {
	res = 0;
	if (memory.contains(addr)) {
		switch (size) {
			case 8:
				res |= static_cast<uint64_t>(memory[addr + 4]) << 32;
				res |= static_cast<uint64_t>(memory[addr + 5]) << 40;
				res |= static_cast<uint64_t>(memory[addr + 6]) << 48;
				res |= static_cast<uint64_t>(memory[addr + 7]) << 56;
				[[fallthrough]];
			case 4:
				res |= memory[addr + 2] << 16 | memory[addr + 3] << 24;
				[[fallthrough]];
			case 2:
				res |= memory[addr + 1] << 8;
				[[fallthrough]];
			case 1:
				res |= memory[addr];
				break;
			default:
				break;
		}
	}
	return qacpi::Status::Success;
}

qacpi::Status qacpi_os_mmio_write(uint64_t addr, uint8_t size, uint64_t value) {
	switch (size) {
		case 8:
			memory[addr + 4] = value >> 32;
			memory[addr + 5] = value >> 40;
			memory[addr + 6] = value >> 48;
			memory[addr + 7] = value >> 56;
			[[fallthrough]];
		case 4:
			memory[addr + 2] = value >> 16;
			memory[addr + 3] = value >> 24;
			[[fallthrough]];
		case 2:
			memory[addr + 1] = value >> 8;
			[[fallthrough]];
		case 1:
			memory[addr] = value;
			break;
		default:
			return qacpi::Status::InvalidArgs;
	}
	return qacpi::Status::Success;
}

qacpi::Status qacpi_os_io_read(uint32_t, uint8_t, uint64_t& res) {
	res = 0xFFFFFFFFFFFFFFFF;
	return qacpi::Status::Success;
}

qacpi::Status qacpi_os_io_write(uint32_t, uint8_t size, uint64_t) {
	switch (size) {
		case 1:
		case 2:
		case 4:
			return qacpi::Status::Success;
		default:
			return qacpi::Status::InvalidArgs;
	}
}

qacpi::Status qacpi_os_pci_read(qacpi::PciAddress, uint64_t offset, uint8_t size, uint64_t& res) {
	return qacpi_os_io_read(offset, size, res);
}

qacpi::Status qacpi_os_pci_write(qacpi::PciAddress, uint64_t, uint8_t, uint64_t) {
	return qacpi::Status::Success;
}

void* qacpi_os_malloc(size_t size) {
	return malloc(size);
}

void qacpi_os_free(void* ptr, size_t) {
	free(ptr);
}

void qacpi_os_trace(const char* str, size_t size) {
	printf("%.*s\n", static_cast<int>(size), str);
}

uint64_t qacpi_os_timer()
{
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;

    if (frequency.QuadPart == 0) {
        if (!QueryPerformanceFrequency(&frequency)) {
            puts("QueryPerformanceFrequency() returned an error");
            std::abort();
        }
    }

    if (!QueryPerformanceCounter(&counter)) {
        puts("QueryPerformanceCounter() returned an error");
        std::abort();
    }

    // Convert to 100 nanoseconds
    counter.QuadPart *= 10000000;
    return counter.QuadPart / frequency.QuadPart;
#elif defined(__APPLE__)
    static struct mach_timebase_info tb;
    static bool initialized;
    uint64_t nanoseconds;

    if (!initialized) {
        if (mach_timebase_info(&tb) != KERN_SUCCESS) {
            puts("mach_timebase_info() returned an error");
            std::abort();
        }
        initialized = true;
    }

    nanoseconds = (mach_absolute_time() * tb.numer) / tb.denom;
    return nanoseconds / 100;
#else
    struct timespec ts {};

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
        puts("clock_gettime() returned an error");
        std::abort();
    }

    return (ts.tv_nsec + ts.tv_sec * 1000000000) / 100;
#endif
}

void qacpi_os_stall(uint64_t us)
{
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

void qacpi_os_sleep(uint64_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

bool qacpi_os_mutex_create(void** handle)
{
    *handle = new std::timed_mutex();
	return true;
}

void qacpi_os_mutex_destroy(void* handle)
{
    auto* mutex = (std::timed_mutex*)handle;
    delete mutex;
}

qacpi::Status qacpi_os_mutex_lock(void* handle, uint16_t timeout_ms)
{
    auto *mutex = (std::timed_mutex*)handle;

    if (timeout_ms == 0)
        return mutex->try_lock() ? qacpi::Status::Success : qacpi::Status::TimeOut;

    if (timeout_ms == 0xFFFF) {
        mutex->lock();
        return qacpi::Status::Success;
    }

    auto ret = mutex->try_lock_for(std::chrono::milliseconds(timeout_ms));
	return ret ? qacpi::Status::Success : qacpi::Status::TimeOut;
}

qacpi::Status qacpi_os_mutex_unlock(void* handle)
{
    auto *mutex = (std::timed_mutex*)handle;

    mutex->unlock();
	return qacpi::Status::Success;
}

class Event {
public:
    void signal()
    {
        std::unique_lock<std::mutex> lock(mutex);
        counter++;
        cv.notify_one();
    }

    void reset()
    {
        std::unique_lock<std::mutex> lock(mutex);
        counter = 0;
    }

    bool wait(uint16_t timeout)
    {
        std::unique_lock<std::mutex> lock(mutex);

        if (counter) {
            counter--;
            return true;
        }

        if (timeout == 0)
            return false;

        if (timeout == 0xFFFF) {
            cv.wait(lock, [this] {
                return counter != 0;
            });
            counter--;
            return true;
        }

        auto wait_res = cv.wait_for(
            lock, std::chrono::milliseconds(timeout),
            [this] { return counter != 0; }
        );
        if (!wait_res)
            return false;

        counter--;
        return true;
    }

private:
    std::mutex mutex;
    std::condition_variable cv;
    size_t counter = 0;
};

bool qacpi_os_event_create(void** handle)
{
    *handle = new Event;
	return true;
}

void qacpi_os_event_destroy(void* handle)
{
    auto *event = (Event*)handle;
    delete event;
}

qacpi::Status qacpi_os_event_wait(void* handle, uint16_t timeout_ms)
{
    auto *event = (Event*)handle;
    return event->wait(timeout_ms) ? qacpi::Status::Success : qacpi::Status::TimeOut;
}

qacpi::Status qacpi_os_event_signal(void* handle)
{
    auto *event = (Event*)handle;
    event->signal();
	return qacpi::Status::Success;
}

qacpi::Status qacpi_os_event_reset(void* handle)
{
    auto *event = (Event*)handle;
	event->reset();
	return qacpi::Status::Success;
}

void* qacpi_os_get_tid() {
	return reinterpret_cast<void*>(pthread_self());
}

void qacpi_os_breakpoint() {
	std::cout << "Ignoring breakpoint" << std::endl;
}

void qacpi_os_fatal(uint8_t type, uint16_t code, uint64_t arg) {
	std::cout << "Fatal firmware error:"
	          << " type: " << std::hex << (int)type
	          << " code: " << std::hex << code
	          << " arg: " << std::hex << arg << std::endl;
}

