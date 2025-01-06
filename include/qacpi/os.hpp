#pragma once
#include <stddef.h>
#include <stdint.h>
#include "status.hpp"
#include "utils.hpp"

bool qacpi_os_mutex_create(void** handle);
void qacpi_os_mutex_destroy(void* handle);
qacpi::Status qacpi_os_mutex_lock(void* handle, uint16_t timeout_ms);
qacpi::Status qacpi_os_mutex_unlock(void* handle);

bool qacpi_os_event_create(void** handle);
void qacpi_os_event_destroy(void* handle);
qacpi::Status qacpi_os_event_wait(void* handle, uint16_t timeout_ms);
qacpi::Status qacpi_os_event_signal(void* handle);
qacpi::Status qacpi_os_event_reset(void* handle);

void qacpi_os_trace(const char* str, size_t size);
void* qacpi_os_get_tid();

void* qacpi_os_malloc(size_t size);
void qacpi_os_free(void* ptr, size_t size);

void qacpi_os_stall(uint64_t us);
void qacpi_os_sleep(uint64_t ms);

void qacpi_os_fatal(uint8_t type, uint16_t code, uint64_t arg);

uint64_t qacpi_os_timer();

void qacpi_os_breakpoint();

void* qacpi_os_map(uintptr_t phys, size_t size);
void qacpi_os_unmap(void* addr, size_t size);

qacpi::Status qacpi_os_io_map(uint64_t base, uint64_t size, void** handle);
void qacpi_os_io_unmap(void* handle);

qacpi::Status qacpi_os_io_read(void* handle, uint64_t offset, uint8_t size, uint64_t& res);
qacpi::Status qacpi_os_io_write(void* handle, uint64_t offset, uint8_t size, uint64_t value);

qacpi::Status qacpi_os_pci_read(qacpi::PciAddress address, uint64_t offset, uint8_t size, uint64_t& res);
qacpi::Status qacpi_os_pci_write(qacpi::PciAddress address, uint64_t offset, uint8_t size, uint64_t value);

namespace qacpi {
	struct NamespaceNode;
}

void qacpi_os_notify(void* notify_arg, qacpi::NamespaceNode* node, uint64_t value);

// optional api only required if event support is used
qacpi::Status qacpi_os_install_sci_handler(uint32_t irq, bool (*handler)(void* arg), void* arg, void** handle);
void qacpi_os_uninstall_sci_handler(uint32_t irq, void* handle);
qacpi::Status qacpi_os_queue_work(qacpi::Status (*fn)(void* arg), void* arg);
