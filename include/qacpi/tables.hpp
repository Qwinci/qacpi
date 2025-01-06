#pragma once
#include <stdint.h>
#include "utils.hpp"

namespace qacpi {
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

	struct [[gnu::packed]] RsdpHeader {
		char signature[8];
		uint8_t checksum;
		char oem_id[6];
		uint8_t revision;
		uint32_t rsdt_address;
		uint32_t length;
		uint64_t xsdt_address;
		uint8_t extended_checksum;
		uint8_t reserved[3];
	};

	struct [[gnu::packed]] RsdtHeader {
		SdtHeader hdr;
		uint32_t entries[];
	};

	struct [[gnu::packed]] XsdtHeader {
		SdtHeader hdr;
		uint64_t entries[];
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

	struct TableSignature {
		char name[4];
		char oem_id[6];
		char oem_table_id[8];
	};

	struct Table {
		void unref() const;

		TableSignature signature;
		union {
			SdtHeader* hdr;
			void* data;
		};
		uintptr_t phys;
		uint32_t size;
		bool allocated_in_buffer;
	};
}
