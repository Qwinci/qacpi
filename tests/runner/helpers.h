#include <string_view>
#include <vector>
#include "qacpi/tables.hpp"

extern bool g_expect_virtual_addresses;

template <typename ExprT>
class ScopeGuard {
public:
	ScopeGuard(ExprT expr)
		: callback(std::move(expr)) {}

	~ScopeGuard() { if (!disarmed) callback(); }

	void disarm() { disarmed = true; }

private:
	ExprT callback;
	bool disarmed { false };
};

struct [[gnu::packed]] full_xsdt {
	struct qacpi::SdtHeader hdr;
	qacpi::Fadt* fadt;
	qacpi::SdtHeader* ssdts[];
};

void set_oem(char (&oemid)[6]);
void set_oem_table_id(char(&oemid_table_id)[8]);

void build_xsdt(
	full_xsdt& xsdt,
	qacpi::RsdpHeader& rsdp,
	std::string_view dsdt_path,
	const std::vector<std::string>& ssdt_paths);

std::pair<void*, size_t>
read_entire_file(std::string_view path, size_t min_size = 0);
