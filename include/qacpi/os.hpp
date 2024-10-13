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

qacpi::Status qacpi_os_mmio_read(uint64_t phys, uint8_t size, uint64_t& res);
qacpi::Status qacpi_os_mmio_write(uint64_t phys, uint8_t size, uint64_t value);

qacpi::Status qacpi_os_io_read(uint32_t port, uint8_t size, uint64_t& res);
qacpi::Status qacpi_os_io_write(uint32_t port, uint8_t size, uint64_t value);

qacpi::Status qacpi_os_pci_read(qacpi::PciAddress address, uint64_t offset, uint8_t size, uint64_t& res);
qacpi::Status qacpi_os_pci_write(qacpi::PciAddress address, uint64_t offset, uint8_t size, uint64_t value);

namespace qacpi {
	struct NamespaceNode;
}

void qacpi_os_notify(qacpi::NamespaceNode* node, uint64_t value);
