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
	else if ((*obj)->get<qacpi::Unresolved>()) {
		return qacpi::ObjectType::Unresolved;
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
		case qacpi::ObjectType::Reserved1:
			return "reserved1";
		case qacpi::ObjectType::Debug:
			return "debug";
		case qacpi::ObjectType::Ref:
			return "ref";
		case qacpi::ObjectType::Arg:
			return "arg";
		case qacpi::ObjectType::Unresolved:
			return "unresolved";
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

template <typename ExprT>
class ScopeGuard
{
public:
    ScopeGuard(ExprT expr)
        : callback(std::move(expr)) {}

    ~ScopeGuard() { callback(); }

private:
    ExprT callback;
};

void qacpi_os_notify(qacpi::NamespaceNode* node, uint64_t value) {
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
    std::string_view dsdt_path, qacpi::ObjectType expected_type,
    std::string_view expected_value
)
{
	auto* dsdt = read_entire_file(dsdt_path);
	ScopeGuard guard {[&] {
		free(dsdt);
	}};
	auto* hdr = static_cast<SdtHeader*>(dsdt);
	uint8_t* aml = reinterpret_cast<uint8_t*>(dsdt) + sizeof(SdtHeader);
	uint32_t aml_size = hdr->length - sizeof(SdtHeader);

    auto ensure_ok_status = [] (qacpi::Status st) {
        if (st == qacpi::Status::Success)
            return;

        auto msg = status_to_str(st);
        throw std::runtime_error(std::string("qacpi error: ") + msg);
    };

	qacpi::Context ctx {hdr->revision, qacpi::LogLevel::Verbose};
	auto st = ctx.init();
	ensure_ok_status(st);

	st = ctx.load_table(aml, aml_size);
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

        run_test(dsdt_path_or_keyword, expected_type, expected_value);
    } catch (const std::exception& ex) {
        std::cerr << "unexpected error: " << ex.what() << std::endl;
        return 1;
    }
}
