#include <iostream>
#include <filesystem>
#include <string>
#include <string_view>
#include "qacpi/context.hpp"
#include "qacpi/ns.hpp"

#include "helpers.h"
#include "argparser.h"

static qacpi::ObjectType string_to_object_type(std::string_view str)
{
    if (str == "int")
        return qacpi::ObjectType::Integer;
    if (str == "str")
        return qacpi::ObjectType::String;

    throw std::runtime_error(
        std::string("Unsupported type for validation: ") + str.data()
    );
}

static qacpi::ObjectType object_get_type(qacpi::ObjectRef* obj) {
	if ((*obj)->get<qacpi::Uninitialized>()) {
		return qacpi::ObjectType::Uninitialized;
	}
	else if ((*obj)->get<uint64_t>()) {
		return qacpi::ObjectType::Integer;
	}
	else if ((*obj)->get<qacpi::String>()) {
		return qacpi::ObjectType::String;
	}
	else if ((*obj)->get<qacpi::Debug>()) {
		return qacpi::ObjectType::Debug;
	}
	else if ((*obj)->get<qacpi::Method>()) {
		return qacpi::ObjectType::Method;
	}
	else if ((*obj)->get<qacpi::Ref>()) {
		return qacpi::ObjectType::Ref;
	}
	else if ((*obj)->get<qacpi::Buffer>()) {
		return qacpi::ObjectType::Buffer;
	}
	else if ((*obj)->get<qacpi::Package>()) {
		return qacpi::ObjectType::Package;
	}
	else if ((*obj)->get<qacpi::Device>()) {
		return qacpi::ObjectType::Device;
	}
	else if ((*obj)->get<qacpi::Mutex>()) {
		return qacpi::ObjectType::Mutex;
	}
	else if ((*obj)->get<qacpi::Event>()) {
		return qacpi::ObjectType::Event;
	}
	else if ((*obj)->get<qacpi::Field>()) {
		return qacpi::ObjectType::Field;
	}
	else if ((*obj)->get<qacpi::OpRegion>()) {
		return qacpi::ObjectType::OpRegion;
	}
	else {
		abort();
	}
}

static std::string_view object_type_to_str(qacpi::ObjectType type) {
	switch (type) {
		case qacpi::ObjectType::Uninitialized:
			return "uninitialized";
		case qacpi::ObjectType::Integer:
			return "integer";
		case qacpi::ObjectType::String:
			return "string";
		case qacpi::ObjectType::Buffer:
			return "buffer";
		case qacpi::ObjectType::Package:
			return "package";
		case qacpi::ObjectType::Field:
			return "field";
		case qacpi::ObjectType::Device:
			return "device";
		case qacpi::ObjectType::Event:
			return "event";
		case qacpi::ObjectType::Method:
			return "method";
		case qacpi::ObjectType::Mutex:
			return "mutex";
		case qacpi::ObjectType::OpRegion:
			return "op region";
		case qacpi::ObjectType::PowerRes:
			return "power resource";
		case qacpi::ObjectType::Processor:
			return "processor";
		case qacpi::ObjectType::ThermalZone:
			return "thermal zone";
		case qacpi::ObjectType::BufferField:
			return "buffer field";
		case qacpi::ObjectType::Reserved:
			return "reserved";
		case qacpi::ObjectType::Debug:
			return "debug";
		case qacpi::ObjectType::Ref:
			return "ref";
		case qacpi::ObjectType::Arg:
			return "arg";
	}
	return "";
}

static void validate_ret_against_expected(
    qacpi::ObjectRef& obj, qacpi::ObjectType expected_type,
    std::string_view expected_val
)
{
    auto ret_is_wrong = [](std::string_view expected, std::string_view actual)
    {
        std::string err;
        err += "returned value '";
        err += actual.data();
        err += "' doesn't match expected '";
        err += expected.data();
        err += "'";

        throw std::runtime_error(err);
    };


    if (object_get_type(&obj) != expected_type) {
        std::string err;
        err += "returned type '";
        err += object_type_to_str(object_get_type(&obj));
        err += "' doesn't match expected '";
        err += object_type_to_str(expected_type);
        err += "'";

        throw std::runtime_error(err);
    }

	auto* ptr = &obj;
	if (auto integer = (*ptr)->get<uint64_t>()) {
		auto expected_int = std::stoull(expected_val.data(), nullptr, 0);
		auto& actual_int = *integer;

		if (expected_int != actual_int)
			ret_is_wrong(expected_val, std::to_string(actual_int));
	}
	else if (auto str = (*ptr)->get<qacpi::String>()) {
		auto actual_str = std::string_view(str->data(), str->size());

		if (expected_val != actual_str)
			ret_is_wrong(expected_val, actual_str);
	}
	else {
		abort();
	}
}

/*
 * DefinitionBlock ("x.aml", "SSDT", 1, "uTEST", "OVERRIDE", 0xF0F0F0F0)
 * {
 *     Name (VAL, "TestRunner")
 * }
 */
uint8_t table_override[] = {
	0x53, 0x53, 0x44, 0x54, 0x35, 0x00, 0x00, 0x00,
	0x01, 0xa1, 0x75, 0x54, 0x45, 0x53, 0x54, 0x00,
	0x4f, 0x56, 0x45, 0x52, 0x52, 0x49, 0x44, 0x45,
	0xf0, 0xf0, 0xf0, 0xf0, 0x49, 0x4e, 0x54, 0x4c,
	0x25, 0x09, 0x20, 0x20, 0x08, 0x56, 0x41, 0x4c,
	0x5f, 0x0d, 0x54, 0x65, 0x73, 0x74, 0x52, 0x75,
	0x6e, 0x6e, 0x65, 0x72, 0x00
};

/*
 * DefinitionBlock ("x.aml", "SSDT", 1, "uTEST", "RUNRIDTB", 0xF0F0F0F0)
 * {
 *     Name (\_SI.TID, "uACPI")
 *     Printf("TestRunner ID SSDT loaded!")
 * }
 */
uint8_t runner_id_table[] = {
	0x53, 0x53, 0x44, 0x54, 0x55, 0x00, 0x00, 0x00,
	0x01, 0x45, 0x75, 0x54, 0x45, 0x53, 0x54, 0x00,
	0x52, 0x55, 0x4e, 0x52, 0x49, 0x44, 0x54, 0x42,
	0xf0, 0xf0, 0xf0, 0xf0, 0x49, 0x4e, 0x54, 0x4c,
	0x25, 0x09, 0x20, 0x20, 0x08, 0x5c, 0x2e, 0x5f,
	0x53, 0x49, 0x5f, 0x54, 0x49, 0x44, 0x5f, 0x0d,
	0x75, 0x41, 0x43, 0x50, 0x49, 0x00, 0x70, 0x0d,
	0x54, 0x65, 0x73, 0x74, 0x52, 0x75, 0x6e, 0x6e,
	0x65, 0x72, 0x20, 0x49, 0x44, 0x20, 0x53, 0x53,
	0x44, 0x54, 0x20, 0x6c, 0x6f, 0x61, 0x64, 0x65,
	0x64, 0x21, 0x00, 0x5b, 0x31
};

void qacpi_os_notify(void*, qacpi::NamespaceNode* node, uint64_t value) {
	auto path = node->absolute_path();
	std::cout << "Received a notification from " << path.data() << " "
	          << std::hex << value << std::endl;
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

static void run_test(
    std::string_view dsdt_path, const std::vector<std::string>& ssdt_paths,
	qacpi::ObjectType expected_type, std::string_view expected_value
)
{
	qacpi::RsdpHeader rsdp {};
	memcpy(&rsdp.signature, "RSD PTR ", 8);
	set_oem(rsdp.oem_id);

	auto xsdt_bytes = sizeof(full_xsdt);
	xsdt_bytes += ssdt_paths.size() * sizeof(qacpi::SdtHeader*);

	auto *xsdt = new (std::calloc(xsdt_bytes, 1)) full_xsdt();
	set_oem(xsdt->hdr.oem_id);
	set_oem_table_id(xsdt->hdr.oem_table_id);

	auto xsdt_delete = ScopeGuard(
		[&xsdt, &ssdt_paths] {
			if (xsdt->fadt) {
				delete[] reinterpret_cast<uint8_t*>(
					static_cast<uintptr_t>(xsdt->fadt->x_dsdt)
				);
				delete xsdt->fadt;
			}

			for (size_t i = 0; i < ssdt_paths.size(); ++i)
				delete[] xsdt->ssdts[i];

			xsdt->~full_xsdt();
			std::free(xsdt);
		}
	);
	build_xsdt(*xsdt, rsdp, dsdt_path, ssdt_paths);

	auto ensure_ok_status = [] (qacpi::Status st) {
		if (st == qacpi::Status::Success)
			return;

		auto msg = status_to_str(st);
		throw std::runtime_error(std::string("qacpi error: ") + msg);
	};

	qacpi::Context ctx {};
	ctx.table_install_handler = [](const qacpi::SdtHeader* hdr, void*& override) {
		if (strncmp(hdr->oem_table_id, "DENYTABL", 8) == 0) {
			return false;
		}
		else if (strncmp(hdr->oem_table_id, "OVERTABL", 8) != 0) {
			return true;
		}

		override = table_override;
		return true;
	};

	auto st = ctx.init(reinterpret_cast<uintptr_t>(&rsdp), qacpi::LogLevel::Verbose);
	ensure_ok_status(st);

	/*
	* Go through all AML tables and manually bump their reference counts here
	* so that they're mapped before the call to uacpi_namespace_load(). The
	* reason we need this is to disambiguate calls to uacpi_kernel_map() with
	* a synthetic physical address (that is actually a virtual address for
	* tables that we constructed earlier) or a real physical address that comes
	* from some operation region or any other AML code or action.
	*/
	const qacpi::Table* tbl;
	ctx.find_table_by_name("DSDT", 0, &tbl);
	for (uint32_t i = 0;; ++i) {
		st = ctx.find_table_by_name("SSDT", i, &tbl);
		if (st != qacpi::Status::Success) {
			break;
		}
	}

	g_expect_virtual_addresses = false;

	st = ctx.load_namespace();
	ensure_ok_status(st);
	st = ctx.init_namespace();
	ensure_ok_status(st);

    if (expected_type == qacpi::ObjectType::Uninitialized) // We're done with emulation mode
		return;

	auto ret = qacpi::ObjectRef::empty();
	st = ctx.evaluate("\\MAIN", ret);
    ensure_ok_status(st);
    validate_ret_against_expected(ret, expected_type, expected_value);
}

int main(int argc, char** argv)
{
	auto args = ArgParser {};
	args.add_positional(
			"dsdt-path-or-keyword",
			"path to the DSDT to run or \"resource-tests\" to run the resource "
			"tests and exit"
		)
		.add_list(
			"expect", 'r', "test mode, evaluate \\MAIN and expect "
			               "<expected_type> <expected_value>"
		)
		.add_list(
			"extra-tables", 'x', "a list of extra SSDTs to load"
		)
		.add_flag(
			"enumerate-namespace", 'd',
			"dump the entire namespace after loading it"
		)
		.add_param(
			"while-loop-timeout", 't',
			"number of seconds to use for the while loop timeout"
		)
		.add_param(
			"log-level", 'l',
			"log level to set, one of: debug, trace, info, warning, error"
		)
		.add_help(
			"help", 'h', "Display this menu and exit",
			[&]() { std::cout << "uACPI test runner:\n" << args; }
		);

    try {
        args.parse(argc, argv);

        auto dsdt_path_or_keyword = args.get("dsdt-path-or-keyword");
        if (dsdt_path_or_keyword == "resource-tests") {
            // run_resource_tests();
            return 0;
        }

        std::string_view expected_value;
        qacpi::ObjectType expected_type = qacpi::ObjectType::Uninitialized;

        if (args.is_set('r')) {
            auto& expect = args.get_list('r');
            if (expect.size() != 2)
                throw std::runtime_error("bad --expect format");

            expected_type = string_to_object_type(expect[0]);
            expected_value = expect[1];
        }

        run_test(dsdt_path_or_keyword, args.get_list_or("extra-tables", {}), expected_type, expected_value);
    } catch (const std::exception& ex) {
        std::cerr << "unexpected error: " << ex.what() << std::endl;
        return 1;
    }
}
