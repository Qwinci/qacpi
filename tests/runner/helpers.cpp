#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <string>

#include "helpers.h"

static uint8_t gen_checksum(void *table, size_t size)
{
	uint8_t *bytes = reinterpret_cast<uint8_t*>(table);
	uint8_t csum = 0;
	size_t i;

	for (i = 0; i < size; ++i)
		csum += bytes[i];

	return 256 - csum;
}

void set_oem(char(&oemid)[6])
{
	memcpy(oemid, "uOEMID", sizeof(oemid));
}

void set_oem_table_id(char(&oemid_table_id)[8])
{
	memcpy(oemid_table_id, "uTESTTBL", sizeof(oemid_table_id));
}

void build_xsdt(
	full_xsdt& xsdt, qacpi::RsdpHeader& rsdp, std::string_view dsdt_path,
	const std::vector<std::string> &ssdt_paths
)
{
	std::vector<std::pair<void*, size_t>> tables(ssdt_paths.size() + 1);
	auto tables_delete = ScopeGuard(
		[&tables] {
			for (auto& table : tables)
				delete[] reinterpret_cast<uint8_t*>(table.first);
		}
	);

	tables[0] = read_entire_file(dsdt_path, sizeof(qacpi::SdtHeader));
	for (size_t i = 0; i < ssdt_paths.size(); ++i)
		tables[1 + i] = read_entire_file(ssdt_paths[i], sizeof(qacpi::SdtHeader));

	for (size_t i = 0; i < tables.size(); ++i) {
		auto& table = tables[i];
		auto *hdr = reinterpret_cast<qacpi::SdtHeader*>(table.first);

		if (hdr->length > table.second) {
			std::string path = i ? ssdt_paths[i - 1] : dsdt_path.data();

			throw std::runtime_error(
				"table " + std::to_string(i) +
				" declares that it's bigger than " + path
			);
		}

		auto *signature = "DSDT";
		if (i > 0) {
			signature = "SSDT";
			xsdt.ssdts[i - 1] = hdr;

			/*
			 * Make the pointer NULL here since this table is now managed by the
			 * XSDT, and it's the caller's responsibility to clean it up.
			 */
			table.first = nullptr;
		}

		memcpy(hdr, signature, 4);

		hdr->checksum = 0;
		hdr->checksum = gen_checksum(hdr, hdr->length);
	}

	tables_delete.disarm();

	auto& fadt = *new qacpi::Fadt {};
	set_oem(fadt.hdr.oem_id);
	set_oem_table_id(fadt.hdr.oem_table_id);

	fadt.hdr.length = sizeof(fadt);
	fadt.hdr.revision = 6;

	fadt.pm1a_cnt_blk = 0xFFEE;
	fadt.pm1_cnt_len = 2;

	fadt.pm1a_evt_blk = 0xDEAD;
	fadt.pm1_evt_len = 4;

	fadt.pm2_cnt_blk = 0xCCDD;
	fadt.pm2_cnt_len = 1;

	fadt.gpe0_blk_len = 0x20;
	fadt.gpe0_blk = 0xDEAD;

	fadt.gpe1_base = 128;
	fadt.gpe1_blk = 0xBEEF;
	fadt.gpe1_blk_len = 0x20;

	fadt.x_dsdt = reinterpret_cast<uintptr_t>(tables[0].first);
	memcpy(fadt.hdr.signature, "FACP",
	       sizeof("FACP") - 1);

	fadt.hdr.checksum = gen_checksum(&fadt, sizeof(fadt));

	xsdt.fadt = &fadt;
	xsdt.hdr.length = sizeof(xsdt) + sizeof(qacpi::SdtHeader*) * ssdt_paths.size();

	auto& dsdt = *reinterpret_cast<qacpi::SdtHeader*>(tables[0].first);
	xsdt.hdr.revision = dsdt.revision;
	memcpy(xsdt.hdr.oem_id, dsdt.oem_id, sizeof(dsdt.oem_id));
	xsdt.hdr.oem_revision = dsdt.oem_revision;

	if constexpr (sizeof(void*) == 4) {
		memcpy(xsdt.hdr.signature, "RSDT",
		       sizeof("RSDT") - 1);

		rsdp.rsdt_address = reinterpret_cast<size_t>(&xsdt);
		rsdp.revision = 1;
		rsdp.checksum = gen_checksum(
			&rsdp, offsetof(qacpi::RsdpHeader, length)
		);
	} else {
		memcpy(xsdt.hdr.signature, "XSDT",
		       sizeof("XSDT") - 1);

		rsdp.xsdt_address = reinterpret_cast<size_t>(&xsdt);
		rsdp.length = sizeof(rsdp);
		rsdp.revision = 2;
		rsdp.checksum = gen_checksum(
			&rsdp, offsetof(qacpi::RsdpHeader, length)
		);
		rsdp.extended_checksum = gen_checksum(&rsdp, sizeof(rsdp));
	}
	xsdt.hdr.checksum = gen_checksum(&xsdt, xsdt.hdr.length);
}

std::pair<void*, size_t>
read_entire_file(std::string_view path, size_t min_size)
{
	size_t file_size = std::filesystem::file_size(path);
	if (file_size < min_size) {
		throw std::runtime_error(
			std::string("file ") + path.data() + " is too small"
		);
	}

	std::ifstream file(path.data(), std::ios::binary);

	if (!file)
		throw std::runtime_error(
			std::string("failed to open file ") + path.data()
		);

	auto* buf = new uint8_t[file_size];
	file.read(reinterpret_cast<char*>(buf), file_size);

	if (!file) {
		delete[] buf;
		throw std::runtime_error(
			std::string("failed to read entire file ") + path.data()
		);
	}

	return { buf, file_size };
}
