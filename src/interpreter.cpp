#include "interpreter.hpp"
#include "aml_ops.hpp"
#include "qacpi/ns.hpp"
#include "logger.hpp"
#include "internal.hpp"

using namespace qacpi;

Status Interpreter::execute(const uint8_t* aml, uint32_t size) {
	auto* frame = frames.push();
	if (!frame) {
		return Status::NoMemory;
	}

	frame->start = aml;
	frame->end = aml + size;
	frame->ptr = aml;
	frame->need_result = false;
	frame->is_method = false;
	frame->type = Frame::Scope;

	return parse();
}

static constexpr OpBlock TERM_ARG_BLOCK {
	.op_count = 2,
	.ops {
		Op::TermArg,
		Op::CallHandler
	},
	.handler = OpHandler::None
};

#define CHECK_EOF if (frame.ptr == frame.end) return Status::UnexpectedEof
#define CHECK_EOF_NUM(num) if (frame.ptr + (num) > frame.end) return Status::UnexpectedEof

static bool is_name_char(uint8_t c) {
	return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
		c == '\\' || c == '^' || c == '_' || c == ParentPrefixChar || c == RootChar ||
		c == DualNamePrefix || c == MultiNamePrefix;
}

NamespaceNode* Interpreter::create_or_get_node(StringView name, Context::SearchFlags flags) {
	return context->create_or_find_node(current_scope, !method_frames.is_empty() ? &method_frames.back() : nullptr, name, flags);
}

static constexpr OpBlock CALL_BLOCK {
	.op_count = 2,
	.ops {
		Op::MethodArgs,
		Op::CallHandler
	},
	.handler = OpHandler::Call
};

Status Interpreter::resolve_object(ObjectRef& object) {
	if (auto unresolved = object->get<Unresolved>()) {
		auto* node = create_or_get_node(unresolved->name, Context::SearchFlags::Search);
		if (!node) {
			return Status::NotFound;
		}
		else if (!node->object) {
			LOG << "qacpi: internal error in resolve_object, node->object is null" << endlog;
			return Status::InternalError;
		}

		object = node->object;
	}
	else {
		__builtin_trap();
	}

	return Status::Success;
}

Status Interpreter::invoke_method(NamespaceNode* node, ObjectRef& res, ObjectRef* args, int arg_count) {
	auto& obj = node->object;
	if (auto method = obj->get<Method>()) {
		if (arg_count != method->arg_count) {
			return Status::InvalidArgs;
		}

		if (method->serialized) {
			if (method->mutex->is_owned_by_thread()) {
				++method->mutex->recursion;
			}
			else {
				method->mutex->lock(0xFFFF);
			}
		}

		auto* new_frame = frames.push();
		if (!new_frame) {
			return Status::NoMemory;
		}

		new_frame->start = method->aml;
		new_frame->end = method->aml + method->size;
		new_frame->ptr = method->aml;
		new_frame->parent_scope = current_scope;
		new_frame->need_result = true;
		new_frame->is_method = true;
		new_frame->type = Frame::Scope;

		auto* method_node = NamespaceNode::create("_MTH");
		if (!method_node) {
			return Status::NoMemory;
		}
		method_node->parent = node->parent;
		current_scope = method_node;

		auto* method_frame = method_frames.push();
		if (!method_frame) {
			return Status::NoMemory;
		}

		method_frame->node_link = method_node;
		method_frame->serialize_mutex = method->mutex;
		for (int i = 0; i < method->arg_count; ++i) {
			ObjectRef arg;
			if (!arg) {
				return Status::NoMemory;
			}

			auto copy = args[i];
			arg->data = Ref {.type = Ref::Arg, .inner {move(copy)}};
			method_frame->args[i] = move(arg);
		}

		auto status = parse();

		if (status == Status::Success && !objects.is_empty()) {
			res = pop_and_unwrap_obj();

			if (!res->node) {
				res->node = node->parent;
			}
		}
		else {
			if (res) {
				res->data = Uninitialized {};
			}
		}
		return status;
	}
	else {
		return Status::InvalidArgs;
	}
}

static constexpr const char* CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXZ";
static constexpr const char* LOWER_CHARS = "0123456789abcdefghijklmnopqrstuvwxz";

static bool int_to_str(uint64_t value, int base, String& res) {
	char buf[22];
	char* ptr = buf + 22;

	do {
		*--ptr = CHARS[value % base];
		value /= base;
	} while (value);

	if (base == 16) {
		*--ptr = 'x';
		*--ptr = '0';
	}

	if (!res.init(ptr, (buf + 22) - ptr)) {
		return false;
	}
	return true;
}

static char char_to_lower(char c) {
	return static_cast<char>(c | 1 << 5);
}

static uint64_t str_to_int(StringView str, unsigned int base) {
	auto data = str.ptr;
	auto len = str.size;

	while (len) {
		if (data[0] <= ' ') {
			++data;
			--len;
		}
		else {
			break;
		}
	}
	bool negate = false;
	if (len) {
		if (*data == '+') {
			++data;
			--len;
		}
		else if (*data == '-') {
			negate = true;
			++data;
			--len;
		}
	}

	if (base == 0 && len >= 2) {
		if (*data == '0' && char_to_lower(data[1]) == 'x') {
			data += 2;
			len -= 2;
			base = 16;
		}
		else if (*data == '0') {
			data += 1;
			len -= 1;
			base = 8;
		}
		else {
			base = 10;
		}
	}
	else {
		base = 10;
	}

	uint64_t res = 0;
	while (len) {
		auto c = char_to_lower(*data);
		if (c < '0' || c > LOWER_CHARS[base - 1]) {
			if (negate) {
				return -res;
			}
			else {
				return res;
			}
		}

		auto char_value = c <= '9' ? (c - '0') : (c - 'a' + 10);
		if (res && (res * base) / res != base) {
			return 0xFFFFFFFFFFFFFFFF;
		}
		res *= base;
		res += char_value;
		++data;
		--len;
	}

	if (negate) {
		return -res;
	}
	else {
		return res;
	}
}

Status Interpreter::handle_name(Interpreter::Frame& frame, bool need_result, bool super_name) {
	--frame.ptr;
	String str;
	if (auto status = parse_name_str(frame, str); status != Status::Success) {
		return status;
	}

	if (str.size() < 4) {
		return Status::InvalidAml;
	}

	auto* node = create_or_get_node(str, Context::SearchFlags::Search);
	if (!node) {
		if (frame.type == Frame::Package) {
			ObjectRef obj;
			if (!obj) {
				return Status::NoMemory;
			}
			obj->data = Unresolved {.name {move(str)}};
			if (!objects.push(move(obj))) {
				return Status::NoMemory;
			}
			return Status::Success;
		}

		if (context->log_level >= LogLevel::Warning) {
			LOG << "qacpi warning: node " << str << " was not found" << endlog;
		}
		return Status::NotFound;
	}
	else if (!node->object) {
		LOG << "qacpi: internal error in handle_name, node->object is null" << endlog;
		return Status::InternalError;
	}

	auto& obj = node->object;
	if (auto method = obj->get<Method>()) {
		if (super_name) {
			if (need_result) {
				auto copy = obj;
				if (!objects.push(move(copy))) {
					return Status::NoMemory;
				}
			}
			return Status::Success;
		}

		if (method->serialized) {
			if (method->mutex->is_owned_by_thread()) {
				++method->mutex->recursion;
			}
			else {
				method->mutex->lock(0xFFFF);
			}
		}

		OpBlockCtx block {
			.block = &CALL_BLOCK,
			.objects_at_start = static_cast<uint32_t>(objects.size()),
			.ip = 0,
			.processed = false,
			.need_result = need_result,
			.as_ref = false
		};
		if (!frame.op_blocks.push(move(block))) {
			return Status::NoMemory;
		}

		if (!objects.push(MethodArgs {
			.method = method,
			.parent_scope = node->parent,
			.remaining = method->arg_count
		})) {
			return Status::NoMemory;
		}
	}
	else {
		if (need_result) {
			auto copy = obj;
			if (!objects.push(move(copy))) {
				return Status::NoMemory;
			}
		}
	}

	return Status::Success;
}

template<typename... Types>
struct overloaded : Types... {
	using Types::operator()...;
};

#define QACPI_MIN(a, b) ((a) < (b) ? (a) : (b))
#define QACPI_MAX(a, b) ((a) > (b) ? (a) : (b))

static ObjectRef& unwrap_internal_refs(ObjectRef& obj) {
	auto ptr = &obj;
	while (auto ref = (*ptr)->data.get<Ref>()) {
		if (ref->type == Ref::RefOf) {
			return *ptr;
		}
		ptr = &ref->inner;
	}
	return *ptr;
}

static ObjectRef& unwrap_refs(ObjectRef& obj) {
	auto ptr = &obj;
	while (auto ref = (*ptr)->data.get<Ref>()) {
		ptr = &ref->inner;
	}

	return *ptr;
}

ObjectRef Interpreter::pop_and_unwrap_obj() {
	auto obj = objects.pop().get_unsafe<ObjectRef>();
	if (!obj) {
		return move(obj);
	}
	return unwrap_internal_refs(obj);
}

Status Interpreter::try_convert(ObjectRef& object, ObjectRef& res, const ObjectType* types, int type_count) {
	auto real = unwrap_refs(object);

	for (int i = 0; i < type_count; ++i) {
		if (static_cast<size_t>(types[i]) == real->data.index()) {
			res = real;
			return Status::Success;
		}
	}

	if (!res) {
		res = ObjectRef {};
		if (!res) {
			return Status::NoMemory;
		}
	}

	auto find_type = [&](ObjectType type) {
		for (int i = 0; i < type_count; ++i) {
			if (types[i] == type) {
				return true;
			}
		}
		return false;
	};

	if (auto buf = real->get<Buffer>()) {
		if (find_type(ObjectType::Integer) && buf->size()) {
			uint32_t to_copy = QACPI_MIN(buf->size(), int_size);
			uint64_t value = 0;
			memcpy(&value, buf->data(), to_copy);
			res->data = value;
			return Status::Success;
		}
		else if (find_type(ObjectType::String)) {
			String str;
			if (!str.init_with_size(buf->size() * 2 + (buf->size() ? (buf->size() - 1) : 0))) {
				return Status::NoMemory;
			}

			auto* data = str.data();
			for (uint32_t i = 0; i < buf->size(); ++i) {
				uint8_t byte = buf->data()[i];

				data[1] = CHARS[byte % 16];
				data[0] = CHARS[byte / 16 % 16];
				data += 2;
				if (i != buf->size() - 1) {
					*data++ = ' ';
				}
			}

			res->data = move(str);
			return Status::Success;
		}
	}
	else if (auto buf_field = real->get<BufferField>()) {
		auto& owner = buf_field->owner->get_unsafe<Buffer>();

		if (find_type(ObjectType::Integer) && buf_field->byte_size <= int_size) {
			uint32_t to_copy = QACPI_MIN(buf_field->byte_size, int_size);
			uint64_t value = 0;
			memcpy(&value, owner.data() + buf_field->byte_offset, to_copy);
			if (buf_field->bit_offset || buf_field->bit_size) {
				uint64_t size_mask = (uint64_t {1} << buf_field->total_bit_size) - 1;
				value >>= buf_field->bit_offset;
				value &= size_mask;
			}
			res->data = value;
			return Status::Success;
		}
		else if (find_type(ObjectType::Buffer)) {
			Buffer buffer;
			if (!buffer.init_with_size(buf_field->byte_size)) {
				return Status::NoMemory;
			}

			auto* data = buffer.data();
			if (buf_field->bit_offset) {
				auto bit_offset_size = buf_field->bit_offset + buf_field->byte_size * 8;
				for (uint32_t i = buf_field->bit_offset; i < bit_offset_size; i += 8) {
					uint8_t shift = i % 8;
					uint8_t byte = owner.data()[buf_field->byte_offset + i / 8] >> shift;
					if (buf_field->byte_offset + i / 8 + 1 < owner.size()) {
						byte |= (owner.data()[buf_field->byte_offset + i / 8 + 1] & ((1 << shift) - 1)) << (8 - shift);
					}
					if (i + 8 >= bit_offset_size && buf_field->bit_size) {
						byte &= (1 << buf_field->bit_size) - 1;
					}

					*data++ = byte;
				}
			}
			else {
				for (uint32_t i = 0; i < buf_field->byte_size - 1; ++i) {
					auto byte = owner.data()[buf_field->byte_offset + i];
					*data++ = byte;
				}
				auto byte = owner.data()[buf_field->byte_offset + buf_field->byte_size - 1];
				if (buf_field->bit_size) {
					byte &= (1 << buf_field->bit_size) - 1;
				}
				*data = byte;
			}

			res->data = move(buffer);
			return Status::Success;
		}
		else if (find_type(ObjectType::String)) {
			String str;
			auto display_bytes = buf_field->byte_size;
			if (!str.init_with_size(display_bytes * 2 + (display_bytes ? (display_bytes - 1) : 0))) {
				return Status::NoMemory;
			}

			auto* data = str.data();
			if (buf_field->bit_offset) {
				auto bit_offset_size = buf_field->bit_offset + buf_field->byte_size * 8;
				for (uint32_t i = buf_field->bit_offset; i < bit_offset_size; i += 8) {
					uint8_t shift = i % 8;
					uint8_t byte = owner.data()[buf_field->byte_offset + i / 8] >> shift;
					if (buf_field->byte_offset + i / 8 + 1 < owner.size()) {
						byte |= (owner.data()[buf_field->byte_offset + i / 8 + 1] & ((1 << shift) - 1)) << (8 - shift);
					}
					if (i + 8 >= bit_offset_size && buf_field->bit_size) {
						byte &= (1 << buf_field->bit_size) - 1;
					}

					data[1] = CHARS[byte % 16];
					data[0] = CHARS[byte / 16 % 16];
					data += 2;
					if (i + 8 < bit_offset_size) {
						*data++ = ' ';
					}
				}
			}
			else {
				for (uint32_t i = 0; i < buf_field->byte_size - 1; ++i) {
					auto byte = owner.data()[buf_field->byte_offset + i];
					data[1] = CHARS[byte % 16];
					data[0] = CHARS[byte / 16 % 16];
					data += 2;
					if (i + 1 < buf_field->byte_size) {
						*data++ = ' ';
					}
				}
				auto byte = owner.data()[buf_field->byte_offset + buf_field->byte_size - 1];
				if (buf_field->bit_size) {
					byte &= (1 << buf_field->bit_size) - 1;
				}
				data[1] = CHARS[byte % 16];
				data[0] = CHARS[byte / 16 % 16];
			}

			res->data = move(str);
			return Status::Success;
		}
	}
	else if (auto field = real->get<Field>()) {
		if (find_type(ObjectType::Integer) && field->bit_size <= int_size * 8) {
			if (auto status = read_field(field, res); status != Status::Success) {
				return status;
			}
			return Status::Success;
		}
		else if (find_type(ObjectType::Buffer)) {
			if (field->bit_size <= int_size * 8) {
				if (auto status = read_field(field, res); status != Status::Success) {
					return status;
				}

				Buffer buffer;
				if (!buffer.init(&res->get_unsafe<uint64_t>(), (field->bit_size + 7) / 8)) {
					return Status::NoMemory;
				}

				res->data = move(buffer);
				return Status::Success;
			}
			else {
				LOG << "qacpi: large field -> buffer is not implemented" << endlog;
				return Status::Unsupported;
			}
		}
		else if (find_type(ObjectType::String)) {
			String str;
			auto display_bytes = (field->bit_size + 7) / 8;
			if (!str.init_with_size(display_bytes * 2 + (display_bytes ? (display_bytes - 1) : 0))) {
				return Status::NoMemory;
			}

			auto* data = str.data();

			if (auto status = read_field(field, res); status != Status::Success) {
				return status;
			}

			uint64_t value = res->get_unsafe<uint64_t>();
			for (uint32_t i = 0; i < display_bytes; ++i) {
				uint8_t byte = value;
				data[1] = CHARS[byte % 16];
				data[0] = CHARS[byte / 16 % 16];
				data += 2;
				if (i + 1 < display_bytes) {
					*data++ = ' ';
				}
				value >>= 8;
			}

			res->data = move(str);
			return Status::Success;
		}
	}
	else if (auto integer = real->get<uint64_t>()) {
		uint64_t value = *integer;
		if (find_type(ObjectType::Buffer)) {
			if ((buf = res->get<Buffer>())) {
				uint32_t copy = QACPI_MIN(buf->size(), int_size);
				memcpy(buf->data(), integer, copy);
				memset(buf->data() + copy, 0, buf->size() - copy);
			}
			else {
				Buffer buffer;
				if (!buffer.init(integer, int_size)) {
					return Status::NoMemory;
				}
				res->data = move(buffer);
			}
			return Status::Success;
		}
		else if (find_type(ObjectType::String)) {
			bool is_ascii = true;
			for (int i = 0; i < int_size; ++i) {
				uint8_t byte = value >> (i * 8);
				if (!byte) {
					break;
				}
				if (byte < 0x21 || byte > 0x7E) {
					is_ascii = false;
					break;
				}
			}

			if (is_ascii) {
				char data[9];
				memcpy(data, integer, int_size);
				data[int_size] = 0;
				String str;
				if (!str.init(data, StringView::const_strlen(data))) {
					return Status::NoMemory;
				}
				res->data = move(str);
			}
			else {
				char tmp_buf[16];
				char* ptr = tmp_buf + 16;
				do {
					*--ptr = LOWER_CHARS[value % 16];
					value /= 16;
				} while (value);
				String str;
				if (!str.init(ptr, (tmp_buf + 16) - ptr)) {
					return Status::NoMemory;
				}
				res->data = move(str);
			}

			return Status::Success;
		}
	}
	else if (auto str = real->get<String>()) {
		if (find_type(ObjectType::Integer)) {
			uint64_t result = 0;
			uint32_t copy = QACPI_MIN(str->size(), int_size);
			memcpy(&result, str->data(), copy);
			res->data = result;
			return Status::Success;
		}
		else if (find_type(ObjectType::Buffer)) {
			Buffer new_buf {};
			if (!new_buf.init(str->data(), str->size() + 1)) {
				return Status::NoMemory;
			}
			res->data = move(new_buf);
			return Status::Success;
		}
	}

	return Status::InvalidArgs;
}

static void debug_output(ObjectRef value) {
	// todo
	value->data.visit(overloaded {
		[](String& str) {
			LOG << "aml debug: " << str << endlog;
		},
		[](uint64_t value) {
			LOG << "aml debug: " << value << endlog;
		},
		[](auto& value) {

		}
	});
}

Status Interpreter::store_to_target(ObjectRef target, ObjectRef value) {
	if (target->get<NullTarget>()) {
		return Status::Success;
	}
	else if (target->get<Debug>()) {
		debug_output(move(value));
		return Status::Success;
	}

	auto& real_value = unwrap_internal_refs(value);

	auto real_target = ObjectRef::empty();
	bool copy_obj = false;
	if (auto ref = target->get<Ref>()) {
		// todo maybe handle package index specifically so that Ref::RefOf can be invalid here

		real_target = unwrap_internal_refs(ref->inner);
		if (auto inner_ref = real_target->get<Ref>()) {
			copy_obj = ref->type == Ref::Arg;
			real_target = unwrap_refs(inner_ref->inner);
		}
		else {
			if (ref->type == Ref::Arg) {
				real_target = target;
			}

			copy_obj = true;
		}
	}
	else {
		real_target = move(target);
	}
	if (real_target->get<Uninitialized>()) {
		copy_obj = true;
	}

	if (auto buf_field = real_target->get<BufferField>()) {
		auto& owner = buf_field->owner->get_unsafe<Buffer>();

		if (buf_field->byte_size <= int_size) {
			auto converted = ObjectRef::empty();
			if (auto status = try_convert(real_value, converted, {ObjectType::Integer}); status != Status::Success) {
				return status;
			}

			uint32_t to_copy = QACPI_MIN(buf_field->byte_size, int_size);

			uint64_t old = 0;
			if (buf_field->bit_offset || buf_field->bit_size) {
				memcpy(&old, owner.data() + buf_field->byte_offset, to_copy);
			}

			if (buf_field->bit_offset || buf_field->bit_size) {
				uint64_t size_mask = (uint64_t {1} << buf_field->total_bit_size) - 1;
				old &= ~(size_mask << buf_field->bit_offset);
				old |= (converted->get_unsafe<uint64_t>() & size_mask) << buf_field->bit_offset;
			}
			else {
				old = converted->get_unsafe<uint64_t>();
			}

			memcpy(owner.data() + buf_field->byte_offset, &old, to_copy);
		}
		else {
			LOG << "qacpi: BufferField writes greater than 8 bytes are not implemented" << endlog;
			return Status::Unsupported;
		}
		return Status::Success;
	}
	else if (auto field = real_target->get<Field>()) {
		auto converted = ObjectRef::empty();
		if (auto status = try_convert(real_value, converted, {ObjectType::Integer, ObjectType::Buffer});
			status != Status::Success) {
			return status;
		}
		if (auto status = write_field(field, converted); status != Status::Success) {
			return status;
		}

		return Status::Success;
	}

	if (copy_obj) {
		if (!real_value->data.clone(real_target->data)) {
			return Status::NoMemory;
		}
		return Status::Success;
	}
	else {
		if (auto str = real_target->get<String>()) {
			auto obj = ObjectRef::empty();
			if (auto status = try_convert(real_value, obj, {ObjectType::String});
				status != Status::Success) {
				return status;
			}
			auto& other_str = obj->get_unsafe<String>();
			auto to_copy = QACPI_MIN(str->size(), other_str.size());
			memcpy(str->data(), other_str.data(), to_copy);
			str->data()[to_copy] = 0;
			return Status::Success;
		}
		else if (auto buf = real_target->get<Buffer>()) {
			auto obj = ObjectRef::empty();
			if (auto status = try_convert(real_value, obj, {ObjectType::Buffer});
				status != Status::Success) {
				return status;
			}
			auto& other_buf = obj->get_unsafe<Buffer>();
			auto to_copy = QACPI_MIN(buf->size(), other_buf.size());
			memcpy(buf->data(), other_buf.data(), to_copy);
			memset(buf->data() + to_copy, 0, buf->size() - to_copy);
			return Status::Success;
		}

		auto obj = ObjectRef::empty();
		if (auto status = try_convert(real_value, obj, {static_cast<ObjectType>(real_target->data.index())});
			status != Status::Success) {
			return status;
		}
		if (&*obj == &*real_value) {
			if (!obj->data.clone(real_target->data)) {
				return Status::NoMemory;
			}
		}
		else {
			real_target->data = move(obj->data);
		}
	}

	return Status::Success;
}

Status Interpreter::parse_name_str(Interpreter::Frame& frame, String& res) {
	CHECK_EOF;

	uint32_t prefix_size = 0;

	auto prefix = reinterpret_cast<const char*>(frame.ptr);
	auto c = static_cast<char>(*frame.ptr);
	if (c == RootChar) {
		prefix_size = 1;
		++frame.ptr;
		CHECK_EOF;
		c = static_cast<char>(*frame.ptr);
	}
	else if (c == ParentPrefixChar) {
		while (c == ParentPrefixChar) {
			++frame.ptr;
			++prefix_size;
			CHECK_EOF;
			c = static_cast<char>(*frame.ptr);
		}
	}

	uint32_t num_segs = 1;
	if (c == 0) {
		++frame.ptr;

		if (!res.init(prefix, prefix_size)) {
			return Status::NoMemory;
		}

		return Status::Success;
	}
	else if (c == DualNamePrefix) {
		++frame.ptr;
		num_segs = 2;
	}
	else if (c == MultiNamePrefix) {
		++frame.ptr;
		CHECK_EOF;
		num_segs = *frame.ptr++;
	}

	CHECK_EOF_NUM(num_segs * 4);
	if (!res.init_with_size(prefix_size + num_segs * 4 + (num_segs - 1))) {
		return Status::NoMemory;
	}

	auto data = res.data();
	memcpy(data, prefix, prefix_size);
	data += prefix_size;
	for (uint32_t i = 0; i < num_segs; ++i) {
		for (uint32_t j = 0; j < 4; ++j) {
			*data++ = static_cast<char>(*frame.ptr++);
		}
		if (i != num_segs - 1) {
			*data++ = '.';
		}
	}

	return Status::Success;
}

Status Interpreter::parse_pkg_len(Interpreter::Frame& frame, PkgLength& res) {
	CHECK_EOF;
	auto* start = frame.ptr;
	auto first = *frame.ptr++;
	uint8_t count = first >> 6;

	uint32_t value;
	if (count == 0) {
		value = first & 0b111111;
	}
	else {
		CHECK_EOF_NUM(count);
		value = first & 0xF;
		for (int i = 0; i < count; ++i) {
			value |= *frame.ptr++ << (4 + i * 8);
		}
	}

	res.start = start;
	res.len = value;

	return Status::Success;
}

Status Interpreter::parse_field(FieldList& list, Frame& frame) {
	uint8_t access_type = list.flags & 0xF;
	bool lock = list.flags >> 4 & 1;
	auto update = static_cast<FieldUpdate>(list.flags >> 5 & 0b11);

	CHECK_EOF;
	auto byte = *frame.ptr;
	// ReservedField := 0x0 PkgLength
	if (byte == 0x0) {
		++frame.ptr;
		PkgLength pkg_len {};
		if (auto status = parse_pkg_len(frame, pkg_len); status != Status::Success) {
			return status;
		}
		list.offset += pkg_len.len;
	}
	// AccessField := 0x1 AccessType AccessAttrib
	// ExtendedAccessField := 0x3 AccessType AccessAttrib AccessLength
	else if (byte == 0x1 || byte == 0x3) {
		++frame.ptr;
		CHECK_EOF;
		auto access_type_byte = *frame.ptr++;
		access_type = access_type_byte & 0xF;

		CHECK_EOF;
		// access_attrib
		++frame.ptr;

		// access_length
		if (byte == 0x3) {
			CHECK_EOF;

			++frame.ptr;
		}
	}
	// ConnectField := <0x2 NameString> | <0x2 BufferData>
	else if (byte == 0x2) {
		++frame.ptr;
		CHECK_EOF;

		if (is_name_char(*frame.ptr)) {
			String str;
			if (auto status = parse_name_str(frame, str); status != Status::Success) {
				return status;
			}

			ObjectRef obj;
			if (!obj) {
				return Status::NoMemory;
			}
			obj->data = Unresolved {.name {move(str)}};
			if (auto status = resolve_object(obj); status != Status::Success) {
				return status;
			}

			if (!objects.push(move(obj))) {
				return Status::NoMemory;
			}
			frames.back().ptr = list.frame.ptr;
			list.connect_field_part2 = true;
		}
		else {
			list.connect_field = true;
		}
	}
	else {
		CHECK_EOF_NUM(4);
		auto* name = reinterpret_cast<const char*>(frame.ptr);
		frame.ptr += 4;

		PkgLength pkg_len {};
		if (auto status = parse_pkg_len(frame, pkg_len); status != Status::Success) {
			return status;
		}

		uint8_t access_size = 0;
		switch (access_type) {
			// AnyAcc
			case 0:
			// ByteAcc
			case 1:
			// BufferAcc
			case 5:
				access_size = 1;
				break;
			// WordAcc
			case 2:
				access_size = 2;
				break;
			// DWordAcc
			case 3:
				access_size = 4;
				break;
			// QWordAcc
			case 4:
				access_size = 8;
				break;
			default:
				LOG << "qacpi error: Reserved field access size" << endlog;
				return Status::Unsupported;
		}

		auto* node = create_or_get_node(StringView {name, 4}, Context::SearchFlags::Create);
		if (!node) {
			return Status::NoMemory;
		}
		else if (node->object) {
			LOG << "qacpi warning: skipping field " << StringView {name, 4}
			    << " because a node with the same name already exists" << endlog;
		}
		else {
			node->parent = current_scope;

			auto connection_copy = list.connection;

			ObjectRef obj;
			if (!obj) {
				return Status::NoMemory;
			}
			if (list.type == Field::Normal) {
				obj->data = Field {
					.type = Field::Normal,
					.owner_index {ObjectRef::empty()},
					.data_bank {ObjectRef::empty()},
					.connection {move(connection_copy)},
					.bit_size = pkg_len.len,
					.bit_offset = list.offset,
					.access_size = access_size,
					.update = update,
					.lock = lock
				};
			}
			else if (list.type == Field::Index) {
				obj->data = Field {
					.type = Field::Index,
					.owner_index {ObjectRef::empty()},
					.data_bank {ObjectRef::empty()},
					.connection {move(connection_copy)},
					.bit_size = pkg_len.len,
					.bit_offset = list.offset,
					.access_size = access_size,
					.update = update,
					.lock = lock
				};
			}
			else if (list.type == Field::Bank) {
				obj->data = Field {
					.type = Field::Bank,
					.owner_index {ObjectRef::empty()},
					.data_bank {ObjectRef::empty()},
					.bank_value = 0,
					.connection {move(connection_copy)},
					.bit_size = pkg_len.len,
					.bit_offset = list.offset,
					.access_size = access_size,
					.update = update,
					.lock = lock
				};
			}

			obj->node = node;
			node->object = move(obj);

			if (!list.nodes.push(node)) {
				return Status::NoMemory;
			}
		}

		list.offset += pkg_len.len;
	}

	return Status::Success;
}

Status Interpreter::handle_op(Interpreter::Frame& frame, const OpBlockCtx& block, bool need_result) {
	switch (block.block->handler) {
		case OpHandler::None:
			break;
		case OpHandler::Store:
		{
			auto target = objects.pop().get_unsafe<ObjectRef>();
			auto value = pop_and_unwrap_obj();

			if (auto status = store_to_target(target, value); status != Status::Success) {
				return status;
			}

			if (need_result) {
				ObjectRef obj;
				if (!obj || !value->data.clone(obj->data) || !objects.push(move(obj))) {
					return Status::NoMemory;
				}
			}
			break;
		}
		case OpHandler::String:
		{
			auto* start = reinterpret_cast<const char*>(frame.ptr);
			while (true) {
				CHECK_EOF;
				auto c = *frame.ptr++;
				if (c == 0) {
					break;
				}
			}
			size_t size = reinterpret_cast<const char*>(frame.ptr) - start - 1;

			String str {};
			if (!str.init(start, size)) {
				return Status::NoMemory;
			}

			ObjectRef obj;
			if (!obj) {
				return Status::NoMemory;
			}
			obj->data = move(str);

			if (!objects.push(move(obj))) {
				return Status::NoMemory;
			}
			break;
		}
		case OpHandler::Debug:
		{
			ObjectRef obj;
			if (!obj) {
				return Status::NoMemory;
			}
			obj->data = Debug {};
			if (!objects.push(move(obj))) {
				return Status::NoMemory;
			}
			break;
		}
		case OpHandler::Concat:
		{
			auto target = objects.pop().get_unsafe<ObjectRef>();
			auto rhs_orig = pop_and_unwrap_obj();
			auto lhs_orig = pop_and_unwrap_obj();

			char buf[20];

			auto concat_object_to_str = [&](ObjectRef& value, String& res) {
				const char* str = "";
				size_t str_size = 0;
				value->data.visit(overloaded {
					[&](auto& value) {
						using type = remove_reference_t<decltype(value)>;
						if constexpr (is_same_v<type, Uninitialized>) {
							str = "[Uninitialized Object]";
							str_size = sizeof("[Uninitialized Object]") - 1;
						}
						else if constexpr (is_same_v<type, uint64_t>) {
							char* ptr = buf + 20;
							uint64_t int_value = value;
							do {
								*--ptr = LOWER_CHARS[int_value % 16];
								int_value /= 16;
							} while (int_value);
							str = ptr;
							str_size = (buf + 20) - ptr;
						}
						else if constexpr (is_same_v<type, String>) {
							str = value.data();
							str_size = value.size();
						}
						else if constexpr (is_same_v<type, Buffer>) {
							// todo display the actual content
							str = "[Buffer]";
							str_size = sizeof("[Buffer]") - 1;
						}
						else if constexpr (is_same_v<type, Package>) {
							str = "[Package]";
							str_size = sizeof("[Package]") - 1;
						}
						else if constexpr (is_same_v<type, Field>) {
							str = "[Field]";
							str_size = sizeof("[Field]") - 1;
						}
						else if constexpr (is_same_v<type, Device>) {
							str = "[Device]";
							str_size = sizeof("[Device]") - 1;
						}
						else if constexpr (is_same_v<type, Event>) {
							str = "[Event]";
							str_size = sizeof("[Event]") - 1;
						}
						else if constexpr (is_same_v<type, Method>) {
							str = "[Control Method]";
							str_size = sizeof("[Control Method]") - 1;
						}
						else if constexpr (is_same_v<type, Mutex>) {
							str = "[Mutex]";
							str_size = sizeof("[Mutex]") - 1;
						}
						else if constexpr (is_same_v<type, OpRegion>) {
							str = "[Operation Region]";
							str_size = sizeof("[Operation Region]") - 1;
						}
						else if constexpr (is_same_v<type, PowerResource>) {
							str = "[Power Resource]";
							str_size = sizeof("[Power Resource]") - 1;
						}
						else if constexpr (is_same_v<type, Processor>) {
							str = "[Processor]";
							str_size = sizeof("[Processor]") - 1;
						}
						else if constexpr (is_same_v<type, ThermalZone>) {
							str = "[Thermal Zone]";
							str_size = sizeof("[Thermal Zone]") - 1;
						}
						else if constexpr (is_same_v<type, BufferField>) {
							str = "[Buffer Field]";
							str_size = sizeof("[Buffer Field]") - 1;
						}
						else if constexpr (is_same_v<type, Unresolved>) {
							LOG << "qacpi: internal error: unresolved object in concat str" << endlog;
							str = "<unresolved>";
							str_size = sizeof("<unresolved>") - 1;
						}
						else if constexpr (is_same_v<type, Debug>) {
							str = "[Debug Object]";
							str_size = sizeof("[Debug Object]") - 1;
						}
						else if constexpr (is_same_v<type, Ref>) {
							str = "[Reference]";
							str_size = sizeof("[Reference]") - 1;
						}
						else if constexpr (is_same_v<type, NullTarget>) {
							str = "[Null Target]";
							str_size = sizeof("[Null Target]") - 1;
						}
						else {
							LOG << "qacpi: internal error: unhandled object in concat str" << endlog;
						}
					}
				});

				if (!res.init(str, str_size)) {
					return false;
				}
				return true;
			};

			auto lhs = ObjectRef::empty();
			auto status = try_convert(
				lhs_orig,
				lhs,
				{ObjectType::Integer, ObjectType::String, ObjectType::Buffer});
			if (status == Status::InvalidArgs) {
				lhs = ObjectRef {};
				if (!lhs) {
					return Status::NoMemory;
				}
				String lhs_str;
				if (!concat_object_to_str(lhs_orig, lhs_str)) {
					return Status::NoMemory;
				}
				lhs->data = move(lhs_str);
			}
			else if (status != Status::Success) {
				return status;
			}

			ObjectRef value;
			if (!value) {
				return Status::NoMemory;
			}
			if (auto lhs_int = lhs->get<uint64_t>()) {
				auto rhs = ObjectRef::empty();
				status = try_convert(
					rhs_orig,
					rhs,
					{ObjectType::Integer});
				if (status != Status::Success) {
					return status;
				}

				auto rhs_int = rhs->get_unsafe<uint64_t>();
				Buffer buffer;
				if (!buffer.init_with_size(int_size * 2)) {
					return Status::NoMemory;
				}
				memcpy(buffer.data(), lhs_int, int_size);
				memcpy(buffer.data() + int_size, &rhs_int, int_size);
				value = ObjectRef {move(buffer)};
				if (!value) {
					return Status::NoMemory;
				}
			}
			else if (auto lhs_str = lhs->get<String>()) {
				auto rhs = ObjectRef::empty();
				status = try_convert(
					rhs_orig,
					rhs,
					{ObjectType::String});
				if (status == Status::InvalidArgs) {
					rhs = ObjectRef {};
					if (!rhs) {
						return Status::NoMemory;
					}
					String rhs_str;
					if (!concat_object_to_str(rhs_orig, rhs_str)) {
						return Status::NoMemory;
					}
					rhs->data = move(rhs_str);
				}
				else if (status != Status::Success) {
					return status;
				}

				auto& rhs_str = rhs->get_unsafe<String>();
				String str;
				if (!str.init_with_size(lhs_str->size() + rhs_str.size())) {
					return Status::NoMemory;
				}
				memcpy(str.data(), lhs_str->data(), lhs_str->size());
				memcpy(str.data() + lhs_str->size(), rhs_str.data(), rhs_str.size());
				value = ObjectRef {move(str)};
				if (!value) {
					return Status::NoMemory;
				}
			}
			else if (auto lhs_buffer = lhs->get<Buffer>()) {
				auto rhs = ObjectRef::empty();
				status = try_convert(
					rhs_orig,
					rhs,
					{ObjectType::Buffer});
				if (status != Status::Success) {
					return status;
				}

				auto& rhs_buf = rhs->get_unsafe<Buffer>();
				Buffer buffer;
				if (!buffer.init_with_size(lhs_buffer->size() + rhs_buf.size())) {
					return Status::NoMemory;
				}
				memcpy(buffer.data(), lhs_buffer->data(), lhs_buffer->size());
				memcpy(buffer.data() + lhs_buffer->size(), rhs_buf.data(), rhs_buf.size());
				value = ObjectRef {move(buffer)};
				if (!value) {
					return Status::NoMemory;
				}
			}

			if ((status = store_to_target(target, value)); status != Status::Success) {
				return status;
			}

			if (need_result) {
				ObjectRef obj;
				if (!objects.push(move(value))) {
					return Status::NoMemory;
				}
			}
			break;
		}
		case OpHandler::Constant:
		{
			auto op = *(frame.ptr - 1);
			Object value;

			if (op == ZeroOp) {
				if (block.as_ref) {
					value.data = NullTarget {};
				}
				else {
					value.data = uint64_t {0};
				}
			}
			else if (op == OneOp) {
				value.data = uint64_t {1};
			}
			else if (op == BytePrefix) {
				CHECK_EOF;
				value.data = uint64_t {*frame.ptr++};
			}
			else if (op == WordPrefix) {
				CHECK_EOF_NUM(2);
				uint16_t val;
				memcpy(&val, frame.ptr, 2);
				frame.ptr += 2;
				value.data = uint64_t {val};
			}
			else if (op == DWordPrefix) {
				CHECK_EOF_NUM(4);
				uint32_t val;
				memcpy(&val, frame.ptr, 4);
				frame.ptr += 4;
				value.data = uint64_t {val};
			}
			else if (op == QWordPrefix) {
				CHECK_EOF_NUM(8);
				uint64_t val;
				memcpy(&val, frame.ptr, 8);
				frame.ptr += 8;
				value.data = val;
			}
			else if (op == OnesOp) {
				value.data = uint64_t {0xFFFFFFFFFFFFFFFF};
			}

			if (need_result) {
				ObjectRef obj;
				if (!obj) {
					return Status::NoMemory;
				}
				*obj = move(value);
				if (!objects.push(move(obj))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::Name:
		{
			auto value = pop_and_unwrap_obj();
			auto name = objects.pop().get_unsafe<String>();

			auto* node = create_or_get_node(name, Context::SearchFlags::Create);
			if (!node) {
				return Status::NoMemory;
			}
			else if (node->object) {
				if (context->log_level >= LogLevel::Warning) {
					LOG << "qacpi warning: ignoring duplicate node " << name << endlog;
				}
				break;
			}
			node->parent = current_scope;

			ObjectRef obj;
			if (!obj || !value->data.clone(obj->data)) {
				return Status::NoMemory;
			}
			obj->node = node;
			node->object = move(obj);

			break;
		}
		case OpHandler::Method:
		{
			auto flags = objects.pop().get_unsafe<PkgLength>().len;
			auto name = objects.pop().get_unsafe<String>();
			auto pkg_len = objects.pop().get_unsafe<PkgLength>();
			uint32_t len = pkg_len.len - (frame.ptr - pkg_len.start);

			CHECK_EOF_NUM(len);

			auto* node = create_or_get_node(name, Context::SearchFlags::Create);
			if (!node) {
				return Status::NoMemory;
			}
			else if (node->object) {
				if (context->log_level >= LogLevel::Warning) {
					LOG << "qacpi warning: ignoring duplicate node " << name << endlog;
				}
				frame.ptr += len;
				break;
			}
			node->parent = current_scope;

			ObjectRef obj;
			if (!obj) {
				return Status::NoMemory;
			}

			bool serialized = flags >> 3 & 1;
			uint8_t sync_level = flags >> 4;

			SharedPtr<Mutex> mutex {SharedPtr<Mutex>::empty()};
			if (serialized) {
				mutex = SharedPtr<Mutex> {};
				if (!mutex) {
					return Status::NoMemory;
				}
				mutex->sync_level = sync_level;
				if (!mutex->init()) {
					return Status::NoMemory;
				}
			}

			obj->data = Method {
				.aml = frame.ptr,
				.mutex {move(mutex)},
				.size = len,
				.arg_count = static_cast<uint8_t>(flags & 0b111),
				.serialized = serialized
			};
			obj->node = node;
			node->object = move(obj);
			frame.ptr += len;

			break;
		}
		case OpHandler::Call:
		{
			auto& args = objects[block.objects_at_start].get_unsafe<MethodArgs>();

			auto* new_frame = frames.push();
			if (!new_frame) {
				return Status::NoMemory;
			}
			new_frame->start = args.method->aml;
			new_frame->end = args.method->aml + args.method->size;
			new_frame->ptr = args.method->aml;
			new_frame->parent_scope = current_scope;
			new_frame->need_result = need_result;
			new_frame->is_method = true;
			new_frame->type = Frame::Scope;

			auto* node = NamespaceNode::create("_MTH");
			if (!node) {
				return Status::NoMemory;
			}
			node->parent = args.parent_scope;
			current_scope = node;

			auto* method_frame = method_frames.push();
			if (!method_frame) {
				return Status::NoMemory;
			}

			method_frame->node_link = node;
			method_frame->serialize_mutex = args.method->mutex;
			for (int i = args.method->arg_count; i > 0; --i) {
				ObjectRef arg_wrapper;
				if (!arg_wrapper) {
					return Status::NoMemory;
				}

				auto real_arg = pop_and_unwrap_obj();

				auto arg = ObjectRef::empty();
				if (!real_arg->get<String>() && !real_arg->get<Buffer>() && !real_arg->get<Package>()) {
					arg = ObjectRef {};
					if (!arg || !real_arg->data.clone(arg->data)) {
						return Status::NoMemory;
					}
				}
				else {
					arg = move(real_arg);
				}

				arg_wrapper->data = Ref {.type = Ref::Arg, .inner {move(arg)}};

				method_frame->args[i - 1] = move(arg_wrapper);
			}

			objects.pop();

			break;
		}
		case OpHandler::Arg:
		case OpHandler::Local:
		{
			ObjectRef* value;

			bool is_local = false;
			if (method_frames.is_empty()) {
				if (block.block->handler == OpHandler::Arg) {
					return Status::InvalidAml;
				}
				auto num = *(frame.ptr - 1) - Local0Op;
				value = &context->global_locals[num];
				is_local = true;
			}
			else {
				auto& method = method_frames.back();

				if (block.block->handler == OpHandler::Arg) {
					auto num = *(frame.ptr - 1) - Arg0Op;
					value = &method.args[num];
				}
				else {
					auto num = *(frame.ptr - 1) - Local0Op;
					value = &method.locals[num];
					is_local = true;
				}
			}

			if (!*value) {
				*value = ObjectRef {};
				if (!*value) {
					return Status::NoMemory;
				}

				auto real_value = ObjectRef {Uninitialized {}};
				if (!real_value) {
					return Status::NoMemory;
				}
				(*value)->data = Ref {
					.type = is_local ? Ref::Local : Ref::Arg,
					.inner {move(real_value)}};
			}

			if (need_result) {
				auto copy = *value;
				if (!objects.push(move(copy))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::CondRefOf:
		{
			auto target = objects.pop().get_unsafe<ObjectRef>();
			auto name = objects.pop().get_unsafe<ObjectRef>();

			bool resolved = false;
			if (name) {
				ObjectRef obj;
				if (!obj) {
					return Status::NoMemory;
				}
				obj->data = Ref {.type = Ref::RefOf, .inner {move(name)}};

				if (auto status = store_to_target(target, obj); status != Status::Success) {
					return status;
				}
				resolved = true;
			}

			if (need_result) {
				ObjectRef obj;
				if (!obj) {
					return Status::NoMemory;
				}
				obj->data = uint64_t {resolved};
				if (!objects.push(move(obj))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::RefOf:
		{
			auto target = objects.pop().get_unsafe<ObjectRef>();

			if (need_result) {
				ObjectRef obj;
				if (!obj) {
					return Status::NoMemory;
				}
				obj->data = Ref {.type = Ref::RefOf, .inner {move(target)}};
				if (!objects.push(move(obj))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::DerefOf:
		{
			auto orig_target = pop_and_unwrap_obj();
			auto& target = unwrap_refs(orig_target);

			if (need_result) {
				ObjectRef obj;
				if (!obj || !target->data.clone(obj->data) || !objects.push(move(obj))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::CopyObject:
		{
			auto target = objects.pop().get_unsafe<ObjectRef>();
			auto value = pop_and_unwrap_obj();

			auto dest_obj = ObjectRef::empty();
			if (auto ref = target->get<Ref>(); ref && ref->type == Ref::Arg) {
				auto unwrapped_target = unwrap_internal_refs(target);
				if (unwrapped_target->get<Ref>()) {
					dest_obj = unwrap_refs(unwrapped_target);
				}
				else {
					dest_obj = move(target);
				}
			}
			else {
				dest_obj = move(target);
			}

			if (!value->data.clone(dest_obj->data)) {
				return Status::NoMemory;
			}

			if (need_result) {
				if (!objects.push(move(target))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::Buffer:
		{
			auto size_value = pop_and_unwrap_obj();
			auto pkg_len = objects.pop().get_unsafe<PkgLength>();
			uint32_t init_len = pkg_len.len - (frame.ptr - pkg_len.start);

			auto obj = ObjectRef::empty();
			if (auto status = try_convert(size_value, obj, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			uint32_t real_size = QACPI_MAX(obj->get_unsafe<uint64_t>(), init_len);

			CHECK_EOF_NUM(init_len);
			if (need_result) {
				Buffer buf;
				if (!buf.init_with_size(real_size)) {
					return Status::NoMemory;
				}
				memcpy(buf.data(), frame.ptr, init_len);
				obj->data = move(buf);
				if (!objects.push(move(obj))) {
					return Status::NoMemory;
				}
			}
			frame.ptr += init_len;

			break;
		}
		case OpHandler::Package:
		{
			auto num_elements = objects[block.objects_at_start - 1].get_unsafe<PkgLength>().len;
			uint32_t num_init_elements = objects.size() - block.objects_at_start;
			uint32_t real_num_elements = QACPI_MAX(num_elements, num_init_elements);

			Package package {};
			if (!package.init(real_num_elements)) {
				return Status::NoMemory;
			}

			for (uint32_t i = num_init_elements; i > 0; --i) {
				package.data->elements[i - 1] = pop_and_unwrap_obj();
			}
			for (uint32_t i = num_init_elements; i < num_elements; ++i) {
				ObjectRef obj {};
				if (!obj) {
					return Status::NoMemory;
				}
				obj->data = Uninitialized {};
				package.data->elements[i] = move(obj);
			}

			objects.pop();
			objects.pop();

			if (need_result) {
				ObjectRef obj {};
				if (!obj) {
					return Status::NoMemory;
				}
				obj->data = move(package);
				if (!objects.push(move(obj))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::Index:
		{
			auto target = objects.pop().get_unsafe<ObjectRef>();
			auto index_val = pop_and_unwrap_obj();
			auto src = pop_and_unwrap_obj();

			auto index_obj = ObjectRef::empty();
			if (auto status = try_convert(index_val, index_obj, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}
			auto index = index_obj->get_unsafe<uint64_t>();

			ObjectRef ref;
			if (!ref) {
				return Status::NoMemory;
			}

			if (auto buffer = src->get<Buffer>()) {
				if (index >= buffer->size()) {
					return Status::InvalidAml;
				}

				ObjectRef field;
				if (!field) {
					return Status::NoMemory;
				}
				field->data = BufferField {
					.owner {move(src)},
					.byte_offset = static_cast<uint32_t>(index),
					.byte_size = 1,
					.total_bit_size = 8,
					.bit_offset = 0,
					.bit_size = 0
				};
				ref->data = Ref {.type = Ref::RefOf, .inner {move(field)}};
			}
			else if (auto str = src->get<String>()) {
				if (index >= str->size()) {
					return Status::InvalidAml;
				}

				ObjectRef field;
				if (!field) {
					return Status::NoMemory;
				}
				field->data = BufferField {
					.owner {move(src)},
					.byte_offset = static_cast<uint32_t>(index),
					.byte_size = 1,
					.total_bit_size = 8,
					.bit_offset = 0,
					.bit_size = 0
				};
				ref->data = Ref {.type = Ref::RefOf, .inner {move(field)}};
			}
			else if (auto package = src->get<Package>()) {
				if (index >= package->data->element_count) {
					return Status::InvalidAml;
				}

				if (package->data->elements[index]->get<Unresolved>()) {
					if (auto status = resolve_object(package->data->elements[index]); status != Status::Success) {
						return status;
					}
				}

				auto copy = package->data->elements[index];
				ref->data = Ref {.type = Ref::RefOf, .inner {move(copy)}};
			}
			else {
				return Status::InvalidAml;
			}

			if (auto status = store_to_target(target, ref); status != Status::Success) {
				return status;
			}

			if (need_result) {
				if (!objects.push(move(ref))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::Alias:
		{
			auto name = objects.pop().get_unsafe<String>();
			auto src = objects.pop().get_unsafe<String>();

			auto* node = create_or_get_node(src, Context::SearchFlags::Search);
			if (!node) {
				if (context->log_level >= LogLevel::Warning) {
					LOG << "qacpi warning: node " << src << " was not found (required by alias "
						<< name << ")" << endlog;
				}
			}

			auto* new_node = create_or_get_node(name, Context::SearchFlags::Create);
			if (!new_node) {
				return Status::NoMemory;
			}
			else if (new_node->object) {
				if (context->log_level >= LogLevel::Warning) {
					LOG << "qacpi warning: ignoring duplicate node " << name << endlog;
				}
				break;
			}
			new_node->parent = current_scope;
			if (node) {
				new_node->object = node->object;
			}
			else {
				ObjectRef obj;
				if (!obj) {
					return Status::NoMemory;
				}
				obj->data = Unresolved {move(src)};
				new_node->object = move(obj);
				new_node->object->node = new_node;
			}
			break;
		}
		case OpHandler::Scope:
		case OpHandler::Device:
		{
			auto name = objects.pop().get_unsafe<String>();
			auto pkg_len = objects.pop().get_unsafe<PkgLength>();
			uint32_t len = pkg_len.len - (frame.ptr - pkg_len.start);

			CHECK_EOF_NUM(len);

			auto* node = create_or_get_node(name, Context::SearchFlags::Search);
			if (block.block->handler == OpHandler::Scope) {
				if (!node) {
					LOG << "qacpi: skipping non-existing scope " << name << endlog;
					frame.ptr += len;
					break;
				}
			}
			else {
				node = create_or_get_node(name, Context::SearchFlags::Create);
			}

			if (!node) {
				return Status::NoMemory;
			}
			else if (node->object && block.block->handler == OpHandler::Device) {
				if (context->log_level >= LogLevel::Warning) {
					LOG << "qacpi warning: ignoring duplicate node " << name << endlog;
				}
				frame.ptr += len;
				break;
			}
			else if (!node->object) {
				if (node->_name[0] != 0) {
					node->parent = current_scope;
				}

				ObjectRef obj;
				if (!obj) {
					return Status::NoMemory;
				}
				obj->data = Device {};
				obj->node = node;
				node->object = move(obj);
			}

			if (len) {
				auto start = frame.ptr;
				auto end = frame.ptr + len;
				frame.ptr += len;

				auto* new_frame = frames.push();
				if (!new_frame) {
					return Status::NoMemory;
				}

				new_frame->start = start;
				new_frame->end = end;
				new_frame->ptr = start;
				new_frame->parent_scope = current_scope;
				new_frame->need_result = false;
				new_frame->is_method = false;
				new_frame->type = Frame::Scope;

				current_scope = node;
			}

			break;
		}
		case OpHandler::External:
		{
			objects.pop_discard();
			objects.pop_discard();
			objects.pop_discard();
			break;
		}
		case OpHandler::Mutex:
		{
			auto flags = objects.pop().get_unsafe<PkgLength>().len;
			auto name = objects.pop().get_unsafe<String>();

			auto* node = create_or_get_node(name, Context::SearchFlags::Create);
			if (!node) {
				return Status::NoMemory;
			}
			else if (node->object) {
				if (context->log_level >= LogLevel::Warning) {
					LOG << "qacpi warning: ignoring duplicate node " << name << endlog;
				}
				break;
			}
			node->parent = current_scope;

			ObjectRef obj;
			if (!obj) {
				return Status::NoMemory;
			}
			Mutex mutex;
			mutex.sync_level = flags & 0xF;
			if (!mutex.init()) {
				return Status::NoMemory;
			}
			obj->data = move(mutex);
			obj->node = node;
			node->object = move(obj);

			break;
		}
		case OpHandler::CreateField:
		{
			auto name = objects.pop().get_unsafe<String>();
			auto num_bits_orig = pop_and_unwrap_obj();
			auto bit_index_orig = pop_and_unwrap_obj();
			auto src_orig = pop_and_unwrap_obj();

			auto num_bits_value = ObjectRef::empty();
			auto bit_index_value = ObjectRef::empty();
			auto src = ObjectRef::empty();
			if (auto status = try_convert(src_orig, src, {ObjectType::Buffer});
				status != Status::Success) {
				return status;
			}
			if (auto status = try_convert(num_bits_orig, num_bits_value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}
			if (auto status = try_convert(bit_index_orig, bit_index_value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}
			uint32_t num_bits = num_bits_value->get_unsafe<uint64_t>();
			uint32_t bit_index = bit_index_value->get_unsafe<uint64_t>();

			if ((bit_index + num_bits + 7) / 8 > src->get_unsafe<Buffer>().size()) {
				return Status::InvalidAml;
			}

			auto* node = create_or_get_node(name, Context::SearchFlags::Create);
			if (!node) {
				return Status::NoMemory;
			}
			else if (node->object) {
				if (context->log_level >= LogLevel::Warning) {
					LOG << "qacpi warning: ignoring duplicate node " << name << endlog;
				}
				break;
			}
			node->parent = current_scope;

			uint32_t byte_size = (num_bits + 7) / 8;
			if (bit_index + num_bits > (bit_index & ~7) + byte_size * 8) {
				++byte_size;
			}

			ObjectRef obj;
			if (!obj) {
				return Status::NoMemory;
			}
			auto copy = src;
			obj->data = BufferField {
				.owner {move(copy)},
				.byte_offset = bit_index / 8,
				.byte_size = byte_size,
				.total_bit_size = num_bits,
				.bit_offset = static_cast<uint8_t>(bit_index % 8),
				.bit_size = static_cast<uint8_t>(num_bits % 8)
			};
			obj->node = node;
			node->object = move(obj);

			break;
		}
		case OpHandler::Event:
		{
			auto name = objects.pop().get_unsafe<String>();

			auto* node = create_or_get_node(name, Context::SearchFlags::Create);
			if (!node) {
				return Status::NoMemory;
			}
			else if (node->object) {
				if (context->log_level >= LogLevel::Warning) {
					LOG << "qacpi warning: ignoring duplicate node " << name << endlog;
				}
				break;
			}
			node->parent = current_scope;

			ObjectRef obj;
			if (!obj) {
				return Status::NoMemory;
			}
			Event event;
			if (!event.init()) {
				return Status::NoMemory;
			}
			obj->data = move(event);
			obj->node = node;
			node->object = move(obj);

			break;
		}
		case OpHandler::Stall:
		{
			auto us_value_orig = pop_and_unwrap_obj();
			auto us_value = ObjectRef::empty();
			if (auto status = try_convert(us_value_orig, us_value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			uint64_t us = us_value->get_unsafe<uint64_t>();
			qacpi_os_stall(us);

			break;
		}
		case OpHandler::Sleep:
		{
			auto ms_value_orig = pop_and_unwrap_obj();
			auto ms_value = ObjectRef::empty();
			if (auto status = try_convert(ms_value_orig, ms_value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			uint64_t ms = ms_value->get_unsafe<uint64_t>();
			qacpi_os_sleep(ms);

			break;
		}
		case OpHandler::Acquire:
		{
			uint16_t timeout_ms = objects.pop().get_unsafe<PkgLength>().len;
			auto name = pop_and_unwrap_obj();

			if (auto mutex = name->get<Mutex>()) {
				if (mutex->is_owned_by_thread()) {
					++mutex->recursion;
				}
				else {
					auto status = mutex->lock(timeout_ms);
					if (status == Status::TimeOut) {
						if (need_result) {
							ObjectRef obj;
							if (!obj) {
								return Status::NoMemory;
							}
							obj->data = uint64_t {1};
							if (!objects.push(move(obj))) {
								return Status::NoMemory;
							}
						}
						break;
					}
					else if (status != Status::Success) {
						return status;
					}

					if (method_frames.is_empty()) {
						mutex->prev = nullptr;
						mutex->next = global_locked_mutexes;
						global_locked_mutexes = mutex;
						if (mutex->next) {
							mutex->next->prev = mutex;
						}
					}
					else {
						auto& method_frame = method_frames.back();
						mutex->prev = nullptr;
						mutex->next = method_frame.mutex_link;
						method_frame.mutex_link = mutex;
						if (mutex->next) {
							mutex->next->prev = mutex;
						}
					}
				}

				if (need_result) {
					ObjectRef obj;
					if (!obj) {
						return Status::NoMemory;
					}
					obj->data = uint64_t {0};
					if (!objects.push(move(obj))) {
						return Status::NoMemory;
					}
				}
			}
			else {
				return Status::InvalidAml;
			}

			break;
		}
		case OpHandler::Signal:
		{
			auto name = pop_and_unwrap_obj();

			if (auto event = name->get<Event>()) {
				if (auto status = event->signal(); status != Status::Success) {
					return status;
				}
			}
			else {
				return Status::InvalidAml;
			}

			break;
		}
		case OpHandler::Wait:
		{
			auto timeout_value_orig = pop_and_unwrap_obj();
			auto name = pop_and_unwrap_obj();

			auto timeout_value = ObjectRef::empty();
			if (auto status = try_convert(timeout_value_orig, timeout_value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			uint64_t timeout_ms = timeout_value->get_unsafe<uint64_t>();
			if (timeout_ms > 0xFFFF) {
				timeout_ms = 0xFFFF;
			}

			if (auto event = name->get<Event>()) {
				auto status = event->wait(timeout_ms);
				if (status == Status::TimeOut) {
					if (need_result) {
						ObjectRef obj;
						if (!obj) {
							return Status::NoMemory;
						}
						obj->data = uint64_t {1};
						if (!objects.push(move(obj))) {
							return Status::NoMemory;
						}
					}
					break;
				}
				else if (status != Status::Success) {
					return status;
				}

				if (need_result) {
					ObjectRef obj;
					if (!obj) {
						return Status::NoMemory;
					}
					obj->data = uint64_t {0};
					if (!objects.push(move(obj))) {
						return Status::NoMemory;
					}
				}
			}
			else {
				return Status::InvalidAml;
			}

			break;
		}
		case OpHandler::Reset:
		{
			auto name = pop_and_unwrap_obj();

			if (auto event = name->get<Event>()) {
				if (auto status = event->reset(); status != Status::Success) {
					return status;
				}
			}
			else {
				return Status::InvalidAml;
			}

			break;
		}
		case OpHandler::Release:
		{
			auto name = pop_and_unwrap_obj();

			if (auto mutex = name->get<Mutex>()) {
				if (!mutex->is_owned_by_thread()) {
					return Status::InvalidAml;
				}
				if (mutex->recursion) {
					--mutex->recursion;
					break;
				}

				if (method_frames.is_empty()) {
					if (mutex->prev) {
						mutex->prev->next = mutex->next;
					}
					else {
						global_locked_mutexes = mutex->next;
					}
					if (mutex->next) {
						mutex->next->prev = mutex->prev;
					}
				}
				else {
					auto& method_frame = method_frames.back();
					if (mutex->prev) {
						mutex->prev->next = mutex->next;
					}
					else {
						method_frame.mutex_link = mutex->next;
					}
					if (mutex->next) {
						mutex->next->prev = mutex->prev;
					}
				}

				if (auto status = mutex->unlock(); status != Status::Success) {
					return status;
				}
			}
			else {
				return Status::InvalidAml;
			}

			break;
		}
		case OpHandler::FromBcd:
		{
			auto target = objects.pop().get_unsafe<ObjectRef>();
			auto value_orig = pop_and_unwrap_obj();

			auto converted_value = ObjectRef::empty();
			if (auto status = try_convert(value_orig, converted_value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			auto value = converted_value->get_unsafe<uint64_t>();

			uint64_t result = 0;
			uint64_t multiplier = 1;
			while (value) {
				uint8_t nybble = value & 0xF;
				result += nybble * multiplier;
				value >>= 4;
				multiplier *= 10;
			}

			ObjectRef obj;
			if (!obj) {
				return Status::NoMemory;
			}
			obj->data = result;
			if (auto status = store_to_target(target, obj); status != Status::Success) {
				return status;
			}

			if (need_result) {
				if (!objects.push(move(obj))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::ToBcd:
		{
			auto target = objects.pop().get_unsafe<ObjectRef>();
			auto value_orig = pop_and_unwrap_obj();

			auto converted_value = ObjectRef::empty();
			if (auto status = try_convert(value_orig, converted_value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			auto value = converted_value->get_unsafe<uint64_t>();

			uint64_t result = 0;
			uint8_t offset = 0;
			while (value) {
				uint8_t nybble = value % 10;
				result |= nybble << offset;
				value /= 10;
				offset += 4;
			}

			ObjectRef obj;
			if (!obj) {
				return Status::NoMemory;
			}
			obj->data = result;
			if (auto status = store_to_target(target, obj); status != Status::Success) {
				return status;
			}

			if (need_result) {
				if (!objects.push(move(obj))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::Revision:
		{
			if (need_result) {
				ObjectRef obj;
				if (!obj) {
					return Status::NoMemory;
				}
				obj->data = uint64_t {2};
				if (!objects.push(move(obj))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::Fatal:
		{
			auto arg_orig = pop_and_unwrap_obj();
			auto code = objects.pop().get_unsafe<PkgLength>().len;
			auto type = objects.pop().get_unsafe<PkgLength>().len;

			auto arg_value = ObjectRef::empty();
			if (auto status = try_convert(arg_orig, arg_value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			auto arg = arg_value->get_unsafe<uint64_t>();

			qacpi_os_fatal(type, code, arg);

			break;
		}
		case OpHandler::Timer:
		{
			if (need_result) {
				ObjectRef obj;
				if (!obj) {
					return Status::NoMemory;
				}
				obj->data = qacpi_os_timer();
				if (!objects.push(move(obj))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::Add:
		case OpHandler::Subtract:
		case OpHandler::Multiply:
		case OpHandler::Shl:
		case OpHandler::Shr:
		case OpHandler::And:
		case OpHandler::Nand:
		case OpHandler::Or:
		case OpHandler::Nor:
		case OpHandler::Xor:
		case OpHandler::Mod:
		{
			auto target = objects.pop().get_unsafe<ObjectRef>();
			auto rhs_orig = pop_and_unwrap_obj();
			auto lhs_orig = pop_and_unwrap_obj();

			auto lhs_value = ObjectRef::empty();
			if (auto status = try_convert(lhs_orig, lhs_value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			auto rhs_value = ObjectRef::empty();
			if (auto status = try_convert(rhs_orig, rhs_value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			auto lhs = lhs_value->get_unsafe<uint64_t>();
			auto rhs = rhs_value->get_unsafe<uint64_t>();

			uint64_t result;
			switch (block.block->handler) {
				case OpHandler::Add:
					result = lhs + rhs;
					break;
				case OpHandler::Subtract:
					result = lhs - rhs;
					break;
				case OpHandler::Multiply:
					result = lhs * rhs;
					break;
				case OpHandler::Shl:
					result = lhs << rhs;
					break;
				case OpHandler::Shr:
					result = lhs >> rhs;
					break;
				case OpHandler::And:
					result = lhs & rhs;
					break;
				case OpHandler::Nand:
					result = ~(lhs & rhs);
					break;
				case OpHandler::Or:
					result = lhs | rhs;
					break;
				case OpHandler::Nor:
					result = ~(lhs | rhs);
					break;
				case OpHandler::Xor:
					result = lhs ^ rhs;
					break;
				case OpHandler::Mod:
					result = lhs % rhs;
					break;
				default:
					break;
			}

			ObjectRef obj;
			if (!obj) {
				return Status::NoMemory;
			}
			obj->data = result;
			if (auto status = store_to_target(target, obj); status != Status::Success) {
				return status;
			}

			if (need_result) {
				if (!objects.push(move(obj))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::Increment:
		case OpHandler::Decrement:
		{
			auto target = objects.pop().get_unsafe<ObjectRef>();

			auto value = ObjectRef::empty();
			if (auto status = try_convert(target, value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			uint64_t result;
			switch (block.block->handler) {
				case OpHandler::Increment:
					result = value->get_unsafe<uint64_t>() + 1;
					break;
				case OpHandler::Decrement:
					result = value->get_unsafe<uint64_t>() - 1;
					break;
				default:
					break;
			}

			ObjectRef obj;
			if (!obj) {
				return Status::NoMemory;
			}
			obj->data = result;
			auto& real_target = unwrap_refs(target);
			if (auto status = store_to_target(real_target, obj); status != Status::Success) {
				return status;
			}

			if (need_result) {
				if (!objects.push(move(obj))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::Divide:
		{
			auto quotient_target = pop_and_unwrap_obj();
			auto remainder_target = pop_and_unwrap_obj();
			auto rhs_orig = pop_and_unwrap_obj();
			auto lhs_orig = pop_and_unwrap_obj();

			auto lhs_value = ObjectRef::empty();
			if (auto status = try_convert(lhs_orig, lhs_value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			auto rhs_value = ObjectRef::empty();
			if (auto status = try_convert(rhs_orig, rhs_value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			auto lhs = lhs_value->get_unsafe<uint64_t>();
			auto rhs = rhs_value->get_unsafe<uint64_t>();

			uint64_t quotient = lhs / rhs;
			uint64_t remainder = lhs % rhs;

			ObjectRef quotient_obj;
			ObjectRef remainder_obj;
			if (!quotient_obj || !remainder_obj) {
				return Status::NoMemory;
			}
			quotient_obj->data = quotient;
			remainder_obj->data = remainder;
			if (auto status = store_to_target(quotient_target, quotient_obj); status != Status::Success) {
				return status;
			}
			if (auto status = store_to_target(remainder_target, remainder_obj); status != Status::Success) {
				return status;
			}

			if (need_result) {
				if (!objects.push(move(quotient_obj))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::Not:
		case OpHandler::FindSetLeftBit:
		case OpHandler::FindSetRightBit:
		{
			auto target = objects.pop().get_unsafe<ObjectRef>();
			auto value_orig = pop_and_unwrap_obj();

			auto value = ObjectRef::empty();
			if (auto status = try_convert(value_orig, value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			auto int_value = value->get_unsafe<uint64_t>();

			uint64_t result;
			switch (block.block->handler) {
				case OpHandler::Not:
					result = ~int_value;
					break;
				case OpHandler::FindSetLeftBit:
				{
					result = 0;
					for (int i = int_size * 8; i > 0; --i) {
						if (int_value & uint64_t {1} << (i - 1)) {
							result = (int_size * 8) - i + 1;
							break;
						}
					}
					break;
				}
				case OpHandler::FindSetRightBit:
				{
					result = 0;
					for (int i = 0; i < int_size * 8; ++i) {
						if (int_value & uint64_t {1} << i) {
							result = i + 1;
							break;
						}
					}
					break;
				}
				default:
					break;
			}

			ObjectRef obj;
			if (!obj) {
				return Status::NoMemory;
			}
			obj->data = result;
			if (auto status = store_to_target(target, obj); status != Status::Success) {
				return status;
			}

			if (need_result) {
				if (!objects.push(move(obj))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::LNot:
		{
			auto value_orig = pop_and_unwrap_obj();

			if (!need_result) {
				break;
			}

			auto value = ObjectRef::empty();
			if (auto status = try_convert(value_orig, value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			ObjectRef obj;
			if (!obj) {
				return Status::NoMemory;
			}
			obj->data = uint64_t {!value->get_unsafe<uint64_t>()};
			if (!objects.push(move(obj))) {
				return Status::NoMemory;
			}

			break;
		}
		case OpHandler::LAnd:
		case OpHandler::LOr:
		case OpHandler::LEqual:
		case OpHandler::LGreater:
		case OpHandler::LLess:
		{
			auto rhs_orig = pop_and_unwrap_obj();
			auto lhs_orig = pop_and_unwrap_obj();

			if (!need_result) {
				break;
			}

			auto lhs_value = ObjectRef::empty();
			if (auto status = try_convert(lhs_orig, lhs_value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			auto rhs_value = ObjectRef::empty();
			if (auto status = try_convert(rhs_orig, rhs_value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			auto lhs = lhs_value->get_unsafe<uint64_t>();
			auto rhs = rhs_value->get_unsafe<uint64_t>();

			uint64_t result;
			switch (block.block->handler) {
				case OpHandler::LAnd:
					result = lhs && rhs;
					break;
				case OpHandler::LOr:
					result = lhs || rhs;
					break;
				case OpHandler::LEqual:
					result = lhs == rhs;
					break;
				case OpHandler::LGreater:
					result = lhs > rhs;
					break;
				case OpHandler::LLess:
					result = lhs < rhs;
					break;
				default:
					break;
			}

			ObjectRef obj;
			if (!obj) {
				return Status::NoMemory;
			}
			obj->data = result;
			if (!objects.push(move(obj))) {
				return Status::NoMemory;
			}

			break;
		}
		case OpHandler::If:
		{
			auto pred_orig = pop_and_unwrap_obj();
			auto pkg_len = objects.pop().get_unsafe<PkgLength>();
			uint32_t len = pkg_len.len - (frame.ptr - pkg_len.start);

			CHECK_EOF_NUM(len);

			auto pred_val = ObjectRef::empty();
			if (auto status = try_convert(pred_orig, pred_val, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			if (pred_val->get_unsafe<uint64_t>()) {
				if (len) {
					auto start = frame.ptr;
					auto end = frame.ptr + len;
					frame.ptr += len;

					auto* new_frame = frames.push();
					if (!new_frame) {
						return Status::NoMemory;
					}

					new_frame->start = start;
					new_frame->end = end;
					new_frame->ptr = start;
					new_frame->parent_scope = nullptr;
					new_frame->need_result = false;
					new_frame->is_method = false;
					new_frame->type = Frame::If;
				}
			}
			else {
				frame.ptr += len;

				if (frame.ptr != frame.end && *frame.ptr == ElseOp) {
					++frame.ptr;
					CHECK_EOF;
					auto first = *frame.ptr++;
					uint8_t count = first >> 6;
					CHECK_EOF_NUM(count);
					frame.ptr += count;
				}
			}

			break;
		}
		case OpHandler::Else:
		{
			auto pkg_len = objects.pop().get_unsafe<PkgLength>();
			uint32_t len = pkg_len.len - (frame.ptr - pkg_len.start);
			CHECK_EOF_NUM(len);

			frame.ptr += len;
			break;
		}
		case OpHandler::While:
		{
			auto pred_orig = pop_and_unwrap_obj();
			auto pkg_len = objects.pop().get_unsafe<PkgLength>();
			uint32_t len = pkg_len.len - (frame.ptr - pkg_len.start);

			CHECK_EOF_NUM(len);

			auto pred_val = ObjectRef::empty();
			if (auto status = try_convert(pred_orig, pred_val, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			if (pred_val->get_unsafe<uint64_t>()) {
				if (len) {
					auto start = frame.ptr;
					auto end = frame.ptr + len;
					frame.ptr = pkg_len.start - 1;

					auto* new_frame = frames.push();
					if (!new_frame) {
						return Status::NoMemory;
					}

					new_frame->start = start;
					new_frame->end = end;
					new_frame->ptr = start;
					new_frame->parent_scope = nullptr;
					new_frame->need_result = false;
					new_frame->is_method = false;
					new_frame->type = Frame::While;
				}
			}
			else {
				frame.ptr += len;
			}

			break;
		}
		case OpHandler::Noop:
			break;
		case OpHandler::Return:
		{
			auto value = pop_and_unwrap_obj();
			if (auto field = value->get<Field>()) {
				if (auto status = read_field(field, value); status != Status::Success) {
					return status;
				}
			}

			if (method_frames.is_empty()) {
				return Status::InvalidAml;
			}

			while (true) {
				auto& frame_iter = frames.back();
				if (!frame_iter.is_method) {
					frames.pop_discard();
				}
				else {
					frame_iter.ptr = frame_iter.end;
					if (frame_iter.need_result) {
						if (!objects.push(move(value))) {
							return Status::NoMemory;
						}
						frame_iter.need_result = false;
					}
					break;
				}
			}

			break;
		}
		case OpHandler::Break:
		{
			while (true) {
				auto& frame_iter = frames.back();
				if (frame_iter.type != Frame::While) {
					frames.pop_discard();
				}
				else {
					if (frames.size() < 2) {
						return Status::InvalidAml;
					}
					auto& other_frame = frames[frames.size() - 2];

					other_frame.ptr = frame_iter.end;
					frame_iter.ptr = frame_iter.end;
					break;
				}
			}

			break;
		}
		case OpHandler::Continue:
		{
			while (true) {
				auto& frame_iter = frames.back();
				if (frame_iter.type != Frame::While) {
					frames.pop_discard();
				}
				else {
					frame_iter.ptr = frame_iter.end;
					break;
				}
			}

			break;
		}
		case OpHandler::BreakPoint:
			qacpi_os_breakpoint();
			break;
		case OpHandler::ToBuffer:
		{
			auto target = objects.pop().get_unsafe<ObjectRef>();
			auto value = pop_and_unwrap_obj();

			auto res = ObjectRef::empty();
			if (auto status = try_convert(value, res, {ObjectType::Buffer});
				status != Status::Success) {
				return status;
			}

			if (auto status = store_to_target(target, res); status != Status::Success) {
				return status;
			}

			if (need_result) {
				if (!objects.push(move(res))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::OpRegion:
		{
			auto len_value_orig = pop_and_unwrap_obj();
			auto offset_value_orig = pop_and_unwrap_obj();
			auto space = objects.pop().get_unsafe<PkgLength>().len;
			auto name = objects.pop().get_unsafe<String>();

			auto len_value = ObjectRef::empty();
			auto offset_value = ObjectRef::empty();
			if (auto status = try_convert(len_value_orig, len_value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}
			if (auto status = try_convert(offset_value_orig, offset_value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			auto len = len_value->get_unsafe<uint64_t>();
			auto offset = offset_value->get_unsafe<uint64_t>();

			auto* node = create_or_get_node(name, Context::SearchFlags::Create);
			if (!node) {
				return Status::NoMemory;
			}
			else if (node->object) {
				if (context->log_level >= LogLevel::Warning) {
					LOG << "qacpi warning: ignoring duplicate node " << name << endlog;
				}
				break;
			}
			node->parent = current_scope;

			auto reg_space = static_cast<RegionSpace>(space);

			ObjectRef obj;
			if (!obj) {
				return Status::NoMemory;
			}
			obj->data = OpRegion {
				.ctx = *context,
				.node = node,
				.offset = offset,
				.size = len,
				.pci_address {},
				.space = reg_space,
				.attached = false,
				.regged = false
			};

			obj->node = node;
			node->object = move(obj);

			if (reg_space != RegionSpace::SystemMemory && reg_space != RegionSpace::SystemIo) {
				bool found = false;
				for (auto* handler = context->region_handlers; handler; handler = handler->next) {
					if (handler->id != reg_space) {
						continue;
					}

					auto status = node->object->get_unsafe<OpRegion>().run_reg();
					if (status == Status::NotFound) {
						if (method_frames.is_empty()) {
							node->prev_link = nullptr;
							node->next_link = context->regions_to_reg;
							if (node->next_link) {
								node->next_link->prev_link = node;
							}
							context->regions_to_reg = node;
						}
					}
					else if (status != Status::Success) {
						LOG << "qacpi error: failed to run _REG for " << name << endlog;
						return status;
					}

					found = true;
					break;
				}

				if (!found && method_frames.is_empty()) {
					node->prev_link = nullptr;
					node->next_link = context->regions_to_reg;
					if (node->next_link) {
						node->next_link->prev_link = node;
					}
					context->regions_to_reg = node;
				}
			}

			break;
		}
		case OpHandler::CreateBitField:
		case OpHandler::CreateByteField:
		case OpHandler::CreateWordField:
		case OpHandler::CreateDWordField:
		case OpHandler::CreateQWordField:
		{
			auto name = objects.pop().get_unsafe<String>();
			auto index_orig = pop_and_unwrap_obj();
			auto src_orig = pop_and_unwrap_obj();

			auto index_value = ObjectRef::empty();
			auto src = ObjectRef::empty();
			if (auto status = try_convert(src_orig, src, {ObjectType::Buffer});
				status != Status::Success) {
				return status;
			}
			if (auto status = try_convert(index_orig, index_value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}
			uint64_t index = index_value->get_unsafe<uint64_t>();

			uint32_t byte_size = 0;
			uint32_t byte_offset = 0;
			uint32_t total_bit_size = 0;
			uint8_t bit_size = 0;
			uint8_t bit_offset = 0;
			switch (block.block->handler) {
				case OpHandler::CreateBitField:
					byte_size = 1;
					byte_offset = index / 8;
					bit_size = 1;
					bit_offset = index % 8;
					total_bit_size = 1;
					break;
				case OpHandler::CreateByteField:
					byte_size = 1;
					byte_offset = index;
					total_bit_size = 8;
					break;
				case OpHandler::CreateWordField:
					byte_size = 2;
					byte_offset = index;
					total_bit_size = 16;
					break;
				case OpHandler::CreateDWordField:
					byte_size = 4;
					byte_offset = index;
					total_bit_size = 32;
					break;
				case OpHandler::CreateQWordField:
					byte_size = 8;
					byte_offset = index;
					total_bit_size = 64;
					break;
				default:
					break;
			}

			if (byte_offset + byte_size > src->get_unsafe<Buffer>().size()) {
				return Status::InvalidAml;
			}

			auto* node = create_or_get_node(name, Context::SearchFlags::Create);
			if (!node) {
				return Status::NoMemory;
			}
			else if (node->object) {
				if (context->log_level >= LogLevel::Warning) {
					LOG << "qacpi warning: ignoring duplicate node " << name << endlog;
				}
				break;
			}
			node->parent = current_scope;

			if (bit_offset + total_bit_size > (bit_offset & ~7) + byte_size * 8) {
				++byte_size;
			}

			ObjectRef obj;
			if (!obj) {
				return Status::NoMemory;
			}
			auto copy = src;
			obj->data = BufferField {
				.owner {move(copy)},
				.byte_offset = byte_offset,
				.byte_size = byte_size,
				.total_bit_size = total_bit_size,
				.bit_offset = bit_offset,
				.bit_size = bit_size
			};
			obj->node = node;
			node->object = move(obj);

			break;
		}
		case OpHandler::Field:
		{
			auto list = objects.pop().get_unsafe<FieldList>();
			// flags
			objects.pop();
			auto reg_name = objects.pop().get_unsafe<String>();
			// length
			objects.pop();

			frame.ptr = list.frame.ptr;

			auto* node = create_or_get_node(reg_name, Context::SearchFlags::Search);
			if (!node || !node->object) {
				LOG << "qacpi error: Operation Region " << reg_name << " doesn't exist" << endlog;
				return Status::InvalidAml;
			}

			auto region = node->object;
			if (region->get<OpRegion>()) {
				for (auto field_node : list.nodes) {
					auto& obj = field_node->object->get_unsafe<Field>();
					auto copy = region;
					obj.owner_index = move(copy);
				}
			}
			else {
				LOG << "qacpi error: node " << reg_name << " is not an Operation Region" << endlog;
				return Status::InvalidAml;
			}

			break;
		}
		case OpHandler::PowerRes:
		{
			auto resource_order = objects.pop().get_unsafe<PkgLength>().len;
			auto system_level = objects.pop().get_unsafe<PkgLength>().len;
			auto name = objects.pop().get_unsafe<String>();
			auto pkg_len = objects.pop().get_unsafe<PkgLength>();
			uint32_t len = pkg_len.len - (frame.ptr - pkg_len.start);

			CHECK_EOF_NUM(len);

			auto* node = create_or_get_node(name, Context::SearchFlags::Create);
			if (!node) {
				return Status::NoMemory;
			}
			else if (node->object) {
				if (context->log_level >= LogLevel::Warning) {
					LOG << "qacpi warning: ignoring duplicate node " << name << endlog;
				}
				frame.ptr += len;
				break;
			}
			else if (!node->object) {
				node->parent = current_scope;

				ObjectRef obj;
				if (!obj) {
					return Status::NoMemory;
				}
				obj->data = PowerResource {
					.resource_order = static_cast<uint16_t>(resource_order),
					.system_level = static_cast<uint8_t>(system_level)
				};
				obj->node = node;
				node->object = move(obj);
			}

			if (len) {
				auto start = frame.ptr;
				auto end = frame.ptr + len;
				frame.ptr += len;

				auto* new_frame = frames.push();
				if (!new_frame) {
					return Status::NoMemory;
				}

				new_frame->start = start;
				new_frame->end = end;
				new_frame->ptr = start;
				new_frame->parent_scope = current_scope;
				new_frame->need_result = false;
				new_frame->is_method = false;
				new_frame->type = Frame::Scope;

				current_scope = node;
			}

			break;
		}
		case OpHandler::Processor:
		{
			auto processor_block_len = objects.pop().get_unsafe<PkgLength>().len;
			auto processor_block_addr = objects.pop().get_unsafe<PkgLength>().len;
			auto processor_id = objects.pop().get_unsafe<PkgLength>().len;
			auto name = objects.pop().get_unsafe<String>();
			auto pkg_len = objects.pop().get_unsafe<PkgLength>();
			uint32_t len = pkg_len.len - (frame.ptr - pkg_len.start);

			CHECK_EOF_NUM(len);

			auto* node = create_or_get_node(name, Context::SearchFlags::Create);
			if (!node) {
				return Status::NoMemory;
			}
			else if (node->object) {
				if (context->log_level >= LogLevel::Warning) {
					LOG << "qacpi warning: ignoring duplicate node " << name << endlog;
				}
				frame.ptr += len;
				break;
			}
			else if (!node->object) {
				node->parent = current_scope;

				ObjectRef obj;
				if (!obj) {
					return Status::NoMemory;
				}
				obj->data = Processor {
					.processor_block_addr = processor_block_addr,
					.processor_block_size = static_cast<uint8_t>(processor_block_len),
					.id = static_cast<uint8_t>(processor_id)
				};
				obj->node = node;
				node->object = move(obj);
			}

			if (len) {
				auto start = frame.ptr;
				auto end = frame.ptr + len;
				frame.ptr += len;

				auto* new_frame = frames.push();
				if (!new_frame) {
					return Status::NoMemory;
				}

				new_frame->start = start;
				new_frame->end = end;
				new_frame->ptr = start;
				new_frame->parent_scope = current_scope;
				new_frame->need_result = false;
				new_frame->is_method = false;
				new_frame->type = Frame::Scope;

				current_scope = node;
			}

			break;
		}
		case OpHandler::ToInteger:
		{
			auto target = objects.pop().get_unsafe<ObjectRef>();
			auto value = pop_and_unwrap_obj();

			auto converted = ObjectRef::empty();
			if (auto status = try_convert(
				value,
				converted,
				{ObjectType::Integer, ObjectType::String, ObjectType::Buffer});
				status != Status::Success) {
				return status;
			}

			ObjectRef res;
			if (!res) {
				return Status::NoMemory;
			}
			if (auto integer = converted->get<uint64_t>()) {
				res->data = *integer;
			}
			else if (auto str = converted->get<String>()) {
				res->data = str_to_int(*str, 0);
			}
			else if (auto buffer = converted->get<Buffer>()) {
				uint64_t int_value = 0;
				memcpy(&int_value, buffer->data(), QACPI_MIN(int_size, buffer->size()));
				res->data = int_value;
			}
			else {
				return Status::InvalidAml;
			}

			if (auto status = store_to_target(target, res); status != Status::Success) {
				return status;
			}

			if (need_result) {
				if (!objects.push(move(res))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::ThermalZone:
		{
			auto name = objects.pop().get_unsafe<String>();
			auto pkg_len = objects.pop().get_unsafe<PkgLength>();
			uint32_t len = pkg_len.len - (frame.ptr - pkg_len.start);

			CHECK_EOF_NUM(len);

			auto* node = create_or_get_node(name, Context::SearchFlags::Create);
			if (!node) {
				return Status::NoMemory;
			}
			else if (node->object) {
				LOG << "qacpi: skipping duplicate node " << name << endlog;
				frame.ptr += len;
				break;
			}
			else if (!node->object) {
				node->parent = current_scope;

				ObjectRef obj;
				if (!obj) {
					return Status::NoMemory;
				}
				obj->data = ThermalZone {};
				obj->node = node;
				node->object = move(obj);
			}

			if (len) {
				auto start = frame.ptr;
				auto end = frame.ptr + len;
				frame.ptr += len;

				auto* new_frame = frames.push();
				if (!new_frame) {
					return Status::NoMemory;
				}

				new_frame->start = start;
				new_frame->end = end;
				new_frame->ptr = start;
				new_frame->parent_scope = current_scope;
				new_frame->need_result = false;
				new_frame->is_method = false;
				new_frame->type = Frame::Scope;

				current_scope = node;
			}

			break;
		}
		case OpHandler::Notify:
		{
			auto value_orig = pop_and_unwrap_obj();
			auto object = pop_and_unwrap_obj();

			auto value = ObjectRef::empty();
			if (auto status = try_convert(value_orig, value, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			qacpi_os_notify(context->notify_arg, object->node, value->get_unsafe<uint64_t>());

			break;
		}
		case OpHandler::SizeOf:
		{
			auto orig_name = objects.pop().get_unsafe<ObjectRef>();
			auto& name = unwrap_refs(orig_name);

			if (need_result) {
				ObjectRef obj;
				if (!obj) {
					return Status::NoMemory;
				}

				if (auto buffer = name->get<Buffer>()) {
					obj->data = uint64_t {buffer->size()};
				}
				else if (auto str = name->get<String>()) {
					obj->data = uint64_t {str->size()};
				}
				else if (auto pkg = name->get<Package>()) {
					obj->data = uint64_t {pkg->data->element_count};
				}
				else {
					return Status::InvalidAml;
				}

				if (!objects.push(move(obj))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::ObjectType:
		{
			auto name = pop_and_unwrap_obj();
			name = unwrap_refs(name);

			if (need_result) {
				ObjectRef obj;
				if (!obj) {
					return Status::NoMemory;
				}
				obj->data = uint64_t {name->data.index() - 1};

				if (!objects.push(move(obj))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::ToDecimalString:
		{
			auto target = objects.pop().get_unsafe<ObjectRef>();
			auto value = pop_and_unwrap_obj();

			auto obj = ObjectRef::empty();
			if (auto status = try_convert(
				value,
				obj,
				{ObjectType::Integer, ObjectType::String, ObjectType::Buffer});
				status != Status::Success) {
				return status;
			}

			ObjectRef res_obj;
			if (!res_obj) {
				return Status::NoMemory;
			}

			String res;
			if (auto integer = obj->get<uint64_t>()) {
				if (!int_to_str(*integer, 10, res)) {
					return Status::NoMemory;
				}
			}
			else if (auto str = obj->get<String>()) {
				if (!res.clone(*str)) {
					return Status::NoMemory;
				}
			}
			else if (auto buffer = obj->get<Buffer>()) {
				uint32_t size = 0;
				for (uint32_t i = 0; i < buffer->size(); ++i) {
					auto byte = buffer->data()[i];
					if (byte < 10) {
						++size;
					}
					else if (byte < 100) {
						size += 2;
					}
					else {
						size += 3;
					}
				}

				size += buffer->size() ? (buffer->size() - 1) : 0;

				if (!res.init_with_size(size)) {
					return Status::NoMemory;
				}

				auto* data = res.data();
				for (uint32_t i = 0; i < buffer->size(); ++i) {
					auto byte = buffer->data()[i];
					char buf[3];
					char* ptr = buf + 3;
					do {
						*--ptr = static_cast<char>('0' + byte % 10);
						byte /= 10;
					} while (byte);
					memcpy(data, ptr, (buf + 3) - ptr);
					data += (buf + 3) - ptr;
					if (i != buffer->size() - 1) {
						*data++ = ',';
					}
				}
			}

			res_obj->data = move(res);

			if (auto status = store_to_target(target, res_obj); status != Status::Success) {
				return status;
			}

			if (need_result) {
				if (!objects.push(move(res_obj))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::ToHexString:
		{
			auto target = objects.pop().get_unsafe<ObjectRef>();
			auto value = pop_and_unwrap_obj();

			auto obj = ObjectRef::empty();
			if (auto status = try_convert(
					value,
					obj,
					{ObjectType::Integer, ObjectType::String, ObjectType::Buffer});
				status != Status::Success) {
				return status;
			}

			ObjectRef res_obj;
			if (!res_obj) {
				return Status::NoMemory;
			}

			String res;
			if (auto integer = obj->get<uint64_t>()) {
				if (!int_to_str(*integer, 16, res)) {
					return Status::NoMemory;
				}
			}
			else if (auto str = obj->get<String>()) {
				if (!res.clone(*str)) {
					return Status::NoMemory;
				}
			}
			else if (auto buffer = obj->get<Buffer>()) {
				uint32_t size = buffer->size() * 4;
				size += buffer->size() ? (buffer->size() - 1) : 0;

				if (!res.init_with_size(size)) {
					return Status::NoMemory;
				}

				auto* data = res.data();
				for (uint32_t i = 0; i < buffer->size(); ++i) {
					auto byte = buffer->data()[i];
					data[0] = '0';
					data[1] = 'x';
					data[2] = CHARS[byte / 16 % 16];
					data[3] = CHARS[byte % 16];
					data += 4;
					if (i != buffer->size() - 1) {
						*data++ = ',';
					}
				}
			}

			res_obj->data = move(res);

			if (auto status = store_to_target(target, res_obj); status != Status::Success) {
				return status;
			}

			if (need_result) {
				if (!objects.push(move(res_obj))) {
					return Status::NoMemory;
				}
			}

			break;
		}
		case OpHandler::DataRegion:
		{
			auto oem_table_id = pop_and_unwrap_obj();
			auto oem_id = pop_and_unwrap_obj();
			auto signature = pop_and_unwrap_obj();
			auto name = objects.pop().get_unsafe<String>();

			// todo implement
			LOG << "qacpi warning: Ignoring DataRegion" << endlog;
			break;
		}
		case OpHandler::IndexField:
		{
			auto list = objects.pop().get_unsafe<FieldList>();
			// flags
			objects.pop();
			auto data_name = objects.pop().get_unsafe<String>();
			auto index_name = objects.pop().get_unsafe<String>();
			// length
			objects.pop();

			frame.ptr = list.frame.ptr;

			auto* index_node = create_or_get_node(index_name, Context::SearchFlags::Search);
			if (!index_node || !index_node->object) {
				LOG << "qacpi error: Node " << index_name << " doesn't exist (needed as IndexField Index)" << endlog;
				return Status::InvalidAml;
			}
			if (!index_node->object->get<Field>()) {
				LOG << "qacpi error: Node " << index_name << " is not a Field" << endlog;
				return Status::InvalidAml;
			}

			auto* data_node = create_or_get_node(data_name, Context::SearchFlags::Search);
			if (!data_node || !data_node->object) {
				LOG << "qacpi error: Node " << data_name << " doesn't exist (needed as IndexField Data)" << endlog;
				return Status::InvalidAml;
			}
			if (!data_node->object->get<Field>()) {
				LOG << "qacpi error: Node " << data_name << " is not a Field" << endlog;
				return Status::InvalidAml;
			}

			auto index_field = index_node->object;
			auto data_field = data_node->object;
			for (auto field_node : list.nodes) {
				auto& obj = field_node->object->get_unsafe<Field>();
				auto index_copy = index_field;
				auto data_copy = data_field;
				obj.owner_index = move(index_copy);
				obj.data_bank = move(data_copy);
			}

			break;
		}
		case OpHandler::BankField:
		{
			auto list = objects.pop().get_unsafe<FieldList>();
			// flags
			objects.pop();
			auto selection = objects.pop().get_unsafe<ObjectRef>();
			auto bank_name = objects.pop().get_unsafe<String>();
			auto reg_name = objects.pop().get_unsafe<String>();
			// length
			objects.pop();

			frame.ptr = list.frame.ptr;

			auto* region_node = create_or_get_node(reg_name, Context::SearchFlags::Search);
			if (!region_node || !region_node->object) {
				LOG << "qacpi error: Node " << reg_name << " doesn't exist (needed as BankField Region)" << endlog;
				return Status::InvalidAml;
			}

			auto* bank_node = create_or_get_node(bank_name, Context::SearchFlags::Search);
			if (!bank_node || !bank_node->object) {
				LOG << "qacpi error: Node " << bank_name << " doesn't exist (needed as BankField Bank)" << endlog;
				return Status::InvalidAml;
			}
			if (!bank_node->object->get<Field>()) {
				LOG << "qacpi error: Node " << bank_name << " is not a Field" << endlog;
				return Status::InvalidAml;
			}

			auto selection_res = ObjectRef::empty();
			if (auto status = try_convert(selection, selection_res, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}

			auto region = region_node->object;
			auto bank = bank_node->object;
			if (region->get<OpRegion>()) {
				for (auto field_node : list.nodes) {
					auto& obj = field_node->object->get_unsafe<Field>();
					auto owner_copy = region;
					auto bank_copy = bank;
					obj.owner_index = move(owner_copy);
					obj.data_bank = move(bank_copy);
					obj.bank_value = selection_res->get_unsafe<uint64_t>();
				}
			}
			else {
				LOG << "qacpi error: node " << reg_name << " is not an Operation Region" << endlog;
				return Status::InvalidAml;
			}
			break;
		}
		case OpHandler::Match:
		{
			auto orig_start_index_obj = pop_and_unwrap_obj();
			auto start_index_obj = ObjectRef::empty();
			if (auto status = try_convert(
				orig_start_index_obj,
				start_index_obj, {ObjectType::Integer});
				status != Status::Success) {
				return status;
			}
			auto start_index = start_index_obj->get_unsafe<uint64_t>();

			auto orig_operand2 = pop_and_unwrap_obj();
			auto op2 = objects.pop().get_unsafe<PkgLength>().len;
			auto orig_operand1 = pop_and_unwrap_obj();
			auto op1 = objects.pop().get_unsafe<PkgLength>().len;
			auto pkg_obj = pop_and_unwrap_obj();
			auto pkg = pkg_obj->get<Package>();
			if (!pkg) {
				return Status::InvalidAml;
			}

			if (start_index >= pkg->data->element_count) {
				return Status::InvalidAml;
			}

			if (!need_result) {
				break;
			}

			auto operand1 = ObjectRef::empty();
			if (auto status = try_convert(
				orig_operand1,
				operand1,
				{ObjectType::Integer, ObjectType::String, ObjectType::Buffer});
				status != Status::Success) {
				return status;
			}
			auto operand2 = ObjectRef::empty();
			if (auto status = try_convert(
				orig_operand2,
				operand2,
				{ObjectType::Integer, ObjectType::String, ObjectType::Buffer});
				status != Status::Success) {
				return status;
			}

			if (!operand1->get<uint64_t>() ||
			    !operand2->get<uint64_t>()) {
				LOG << "qacpi error: unsupported operand type for Match" << endlog;
				return Status::Unsupported;
			}

			ObjectRef res_obj;
			if (!res_obj) {
				return Status::NoMemory;
			}

			uint64_t ret_index = 0xFFFFFFFFFFFFFFFF;

			for (uint32_t i = start_index; i < pkg->data->element_count; ++i) {
				auto& element = pkg->data->elements[i];
				auto converted = ObjectRef::empty();
				auto status = try_convert(
					element,
					converted,
					{ObjectType::Integer});
				if (status == Status::InvalidArgs) {
					continue;
				}
				else if (status != Status::Success) {
					return status;
				}

				bool match1;
				switch (op1) {
					case 0:
						match1 = true;
						break;
					case 1:
						match1 = converted->get_unsafe<uint64_t>() == operand1->get_unsafe<uint64_t>();
						break;
					case 2:
						match1 = converted->get_unsafe<uint64_t>() <= operand1->get_unsafe<uint64_t>();
						break;
					case 3:
						match1 = converted->get_unsafe<uint64_t>() < operand1->get_unsafe<uint64_t>();
						break;
					case 4:
						match1 = converted->get_unsafe<uint64_t>() >= operand1->get_unsafe<uint64_t>();
						break;
					case 5:
						match1 = converted->get_unsafe<uint64_t>() > operand1->get_unsafe<uint64_t>();
						break;
					default:
						return Status::InvalidAml;
				}

				if (!match1) {
					continue;
				}

				bool match2;
				switch (op2) {
					case 0:
						match2 = true;
						break;
					case 1:
						match2 = converted->get_unsafe<uint64_t>() == operand2->get_unsafe<uint64_t>();
						break;
					case 2:
						match2 = converted->get_unsafe<uint64_t>() <= operand2->get_unsafe<uint64_t>();
						break;
					case 3:
						match2 = converted->get_unsafe<uint64_t>() < operand2->get_unsafe<uint64_t>();
						break;
					case 4:
						match2 = converted->get_unsafe<uint64_t>() >= operand2->get_unsafe<uint64_t>();
						break;
					case 5:
						match2 = converted->get_unsafe<uint64_t>() > operand2->get_unsafe<uint64_t>();
						break;
					default:
						return Status::InvalidAml;
				}

				if (match2) {
					ret_index = i;
					break;
				}
			}

			res_obj->data = ret_index;

			if (!objects.push(move(res_obj))) {
				return Status::NoMemory;
			}

			break;
		}
	}

	return Status::Success;
}

Status Interpreter::parse() {
	while (true) {
		if (frames.is_empty()) {
			if (objects.size() != 0 && objects.size() != 1) {
				LOG << "qacpi internal error: object stack is not empty after all frames" << endlog;
				return Status::InternalError;
			}
			if (!method_frames.is_empty()) {
				LOG << "qacpi internal error: method frame stack is not empty after all frames" << endlog;
				return Status::InternalError;
			}
			return Status::Success;
		}

		auto& frame = frames.back();
		auto frame_index = frames.size() - 1;
		if (frame.op_blocks.is_empty()) {
			if (frame.ptr == frame.end) {
				if (frame.type == Frame::Scope) {
					current_scope = frame.parent_scope;
				}
				if (frame.is_method) {
					method_frames.pop_discard();
					if (frame.need_result) {
						ObjectRef obj;
						if (!obj) {
							return Status::NoMemory;
						}
						obj->data = uint64_t {0};

						if (!objects.push(move(obj))) {
							return Status::NoMemory;
						}
					}
				}

				frames.pop_discard();
				continue;
			}

			CHECK_EOF;
			auto byte = *frame.ptr++;

			const OpBlock* block;
			if (byte == 0x5B) {
				CHECK_EOF;
				byte = *frame.ptr++;
				block = &EXT_OPS[byte];
			}
			else if (is_name_char(byte)) {
				auto is_package = frame.type == Frame::Package;
				if (auto status = handle_name(frame, is_package, is_package); status != Status::Success) {
					return status;
				}
				continue;
			}
			else {
				block = &OPS[byte];
			}

			if (block->handler == OpHandler::None) {
				LOG << "qacpi internal error: unimplemented op " << byte << endlog;
				return Status::Unsupported;
			}

			if (!frame.op_blocks.push({
				.block = block,
				.objects_at_start = static_cast<uint32_t>(objects.size()),
				.ip = 0,
				.processed = false,
				.need_result = frame.type == Frame::Package,
				.as_ref = false
			})) {
				return Status::NoMemory;
			}
		}

		auto& block = frame.op_blocks.back();
		auto op = block.block->ops[block.ip];

		if (block.processed) {
			++block.ip;
			block.processed = false;
			switch (op) {
				case Op::PkgLength:
				case Op::Byte:
				case Op::Word:
				case Op::DWord:
				{
					++block.objects_at_start;
					if (objects.size() != block.objects_at_start) {
						return Status::InvalidAml;
					}
					if (!objects.back().get<PkgLength>()) {
						__builtin_trap();
					}

					break;
				}
				case Op::NameString:
				{
					++block.objects_at_start;
					if (objects.size() != block.objects_at_start) {
						return Status::InvalidAml;
					}
					if (!objects.back().get<String>()) {
						__builtin_trap();
					}

					break;
				}
				case Op::PkgElements:
				case Op::VarPkgElements:
					break;
				case Op::TermArg:
				case Op::SuperName:
				case Op::SuperNameUnresolved:
					++block.objects_at_start;
					if (objects.size() != block.objects_at_start) {
						return Status::InvalidAml;
					}
					if (!objects.back().get<ObjectRef>()) {
						__builtin_trap();
					}

					break;
				case Op::MethodArgs:
				{
					auto& args = objects[block.objects_at_start].get_unsafe<MethodArgs>();
					if (args.remaining || objects.size() != block.objects_at_start + 1 + args.method->arg_count) {
						return Status::InvalidAml;
					}
					break;
				}
				case Op::FieldList:
				{
					auto& list = objects.back().get_unsafe<FieldList>();
					if (list.frame.ptr != list.frame.end) {
						return Status::InvalidAml;
					}
					break;
				}
				case Op::StartFieldList:
				case Op::CallHandler:
					break;
			}
		}
		else if (op == Op::CallHandler) {
			++block.ip;
			if (auto status = handle_op(frame, block, block.need_result); status != Status::Success) {
				return status;
			}
			frames[frame_index].op_blocks.pop_discard();
		}
		else {
			block.processed = true;

			switch (op) {
				case Op::PkgLength:
				{
					PkgLength res {};
					if (auto status = parse_pkg_len(frame, res); status != Status::Success) {
						return status;
					}

					if (!objects.push(move(res))) {
						return Status::NoMemory;
					}

					break;
				}
				case Op::NameString:
				{
					String str;
					if (auto status = parse_name_str(frame, str); status != Status::Success) {
						return status;
					}
					if (!objects.push(move(str))) {
						return Status::NoMemory;
					}
					break;
				}
				case Op::Byte:
				{
					CHECK_EOF;
					auto byte = *frame.ptr++;

					if (!objects.push(PkgLength {
						.start = frame.ptr,
						.len = byte
					})) {
						return Status::NoMemory;
					}

					break;
				}
				case Op::Word:
				{
					CHECK_EOF_NUM(2);
					uint16_t value;
					memcpy(&value, frame.ptr, 2);
					frame.ptr += 2;

					if (!objects.push(PkgLength {
						.start = frame.ptr,
						.len = value
					})) {
						return Status::NoMemory;
					}

					break;
				}
				case Op::DWord:
				{
					CHECK_EOF_NUM(4);
					uint32_t value;
					memcpy(&value, frame.ptr, 4);
					frame.ptr += 4;

					if (!objects.push(PkgLength {
						.start = frame.ptr,
						.len = value
					})) {
						return Status::NoMemory;
					}

					break;
				}
				case Op::PkgElements:
				case Op::VarPkgElements:
				{
					auto pkg_len = objects[block.objects_at_start - 2].get_unsafe<PkgLength>();
					uint32_t len = pkg_len.len - (frame.ptr - pkg_len.start);

					CHECK_EOF_NUM(len);

					if (op == Op::VarPkgElements) {
						auto num_elements_obj = pop_and_unwrap_obj();

						auto obj = ObjectRef::empty();
						if (auto status = try_convert(num_elements_obj, obj, {ObjectType::Integer});
							status != Status::Success) {
							return status;
						}
						if (!objects.push(PkgLength {
							.start = nullptr,
							.len = static_cast<uint32_t>(obj->get_unsafe<uint64_t>())
						})) {
							return Status::NoMemory;
						}
					}

					auto start = frame.ptr;
					auto end = frame.ptr + len;
					frame.ptr += len;

					auto* new_frame = frames.push();
					if (!new_frame) {
						return Status::NoMemory;
					}

					new_frame->start = start;
					new_frame->end = end;
					new_frame->ptr = start;
					new_frame->parent_scope = current_scope;
					new_frame->need_result = true;
					new_frame->is_method = false;
					new_frame->type = Frame::Package;

					break;
				}
				case Op::MethodArgs:
				{
					auto& args = objects[block.objects_at_start].get_unsafe<MethodArgs>();
					if (!args.remaining) {
						break;
					}
					else {
						--args.remaining;
						block.processed = false;
					}

					[[fallthrough]];
				}
				case Op::TermArg:
				case Op::SuperName:
				case Op::SuperNameUnresolved:
				{
					CHECK_EOF;
					auto byte = *frame.ptr++;

					const OpBlock* new_block;
					if (byte == 0x5B) {
						CHECK_EOF;
						byte = *frame.ptr++;
						new_block = &EXT_OPS[byte];
					}
					else if (is_name_char(byte)) {
						if (auto status = handle_name(
							frame,
							true, op == Op::SuperName || op == Op::SuperNameUnresolved);
							status != Status::Success) {
							if (status == Status::NotFound && op == Op::SuperNameUnresolved) {
								auto obj = ObjectRef::empty();
								if (!objects.push(move(obj))) {
									return Status::NoMemory;
								}
								break;
							}
							return status;
						}
						break;
					}
					else {
						new_block = &OPS[byte];
					}

					if (new_block->handler == OpHandler::None) {
						LOG << "qacpi internal error: unimplemented op " << byte << endlog;
						return Status::Unsupported;
					}

					if (!frame.op_blocks.push({
						.block = new_block,
						.objects_at_start = static_cast<uint32_t>(objects.size()),
						.ip = 0,
						.processed = false,
						.need_result = true,
						.as_ref = op == Op::SuperName
					})) {
						return Status::NoMemory;
					}

					break;
				}
				case Op::StartFieldList:
				{
					uint32_t remaining_data;
					uint8_t flags = objects[objects.size() - 1].get_unsafe<PkgLength>().len;
					decltype(Field::Normal) type;

					if (block.block->handler == OpHandler::Field) {
						auto len = objects[objects.size() - 3].get_unsafe<PkgLength>();
						remaining_data = len.len - (frame.ptr - len.start);
						type = Field::Normal;
					}
					else if (block.block->handler == OpHandler::IndexField) {
						auto len = objects[objects.size() - 4].get_unsafe<PkgLength>();
						remaining_data = len.len - (frame.ptr - len.start);
						type = Field::Index;
					}
					else if (block.block->handler == OpHandler::BankField) {
						auto len = objects[objects.size() - 5].get_unsafe<PkgLength>();
						remaining_data = len.len - (frame.ptr - len.start);
						type = Field::Bank;
					}
					else {
						__builtin_unreachable();
					}

					CHECK_EOF_NUM(remaining_data);

					if (!objects.push(FieldList {
						.nodes {},
						.connection {ObjectRef::empty()},
						.offset = 0,
						.frame {
							.start = frame.ptr,
							.end = frame.ptr + remaining_data,
							.ptr = frame.ptr,
							.parent_scope = nullptr,
							.op_blocks {},
							.need_result = false,
							.is_method = false,
							.type = Frame::FieldList
						},
						.type = type,
						.flags = flags,
						.connect_field = false,
						.connect_field_part2 = false
					})) {
						return Status::NoMemory;
					}
					break;
				}
				case Op::FieldList:
				{
					auto& list = objects[block.objects_at_start].get_unsafe<FieldList>();
					if (list.connect_field) {
						frame.ptr = list.frame.ptr;
						block.processed = false;
						if (!frame.op_blocks.push(OpBlockCtx {
							.block = &TERM_ARG_BLOCK,
							.objects_at_start = static_cast<uint32_t>(objects.size()),
							.ip = 0,
							.processed = false,
							.need_result = true,
							.as_ref = false
						})) {
							return Status::NoMemory;
						}

						list.connect_field = false;
						list.connect_field_part2 = true;
						break;
					}
					else if (list.connect_field_part2) {
						auto connection = objects.pop().get_unsafe<ObjectRef>();
						list.frame.ptr = frame.ptr;
						list.connection = move(connection);
						list.connect_field_part2 = false;
					}

					if (list.frame.ptr == list.frame.end) {
						break;
					}
					else {
						if (auto status = parse_field(list, list.frame); status != Status::Success) {
							return status;
						}

						block.processed = false;
					}
					break;
				}
				case Op::CallHandler:
					__builtin_unreachable();
			}
		}
	}
}

Interpreter::~Interpreter() {
	auto mutex = global_locked_mutexes;
	while (mutex) {
		LOG << "qacpi warning: some mutexes were not unlocked at the end of the global scope" << endlog;
		mutex->unlock();
		mutex = mutex->next;
	}
}

Status Interpreter::read_field(Field* field, ObjectRef& dest) {
	if (field->bit_size > 64) {
		LOG << "qacpi error: Field sizes greater than 8 bytes are not supported" << endlog;
		return Status::Unsupported;
	}
	else {
		uint64_t dest_value = 0;

		uint32_t byte_offset = (field->bit_offset & ~((field->access_size * 8) - 1)) / 8;
		for (uint32_t i = 0; i < field->bit_size;) {
			uint32_t bit_offset = (field->bit_offset + i) & ((field->access_size * 8) - 1);
			uint32_t bits = QACPI_MIN(field->bit_size - i, (field->access_size * 8) - bit_offset);

			uint64_t value = 0;
			if (field->type == Field::Normal || field->type == Field::Bank) {
				if (field->type == Field::Bank) {
					ObjectRef bank_value;
					if (!bank_value) {
						return Status::NoMemory;
					}
					bank_value->data = field->bank_value;
					if (auto status = write_field(&field->data_bank->get_unsafe<Field>(), bank_value);
						status != Status::Success) {
						return status;
					}
				}

				auto& region = field->owner_index->get_unsafe<OpRegion>();
				if (auto status = region.read(byte_offset, field->access_size, value);
					status != Status::Success) {
					return status;
				}
			}
			else {
				auto& index = field->owner_index->get_unsafe<Field>();
				auto& data = field->data_bank->get_unsafe<Field>();

				ObjectRef offset;
				if (!offset) {
					return Status::NoMemory;
				}
				offset->data = uint64_t {byte_offset};
				if (auto status = write_field(&index, offset);
					status != Status::Success) {
					return status;
				}

				ObjectRef value_obj;
				if (!value_obj) {
					return Status::NoMemory;
				}

				if (auto status = read_field(&data, value_obj);
					status != Status::Success) {
					return status;
				}
				if (!value_obj->get<uint64_t>()) {
					LOG << "qacpi error: IndexField Data field with size greater than 8 bytes is not supported" << endlog;
					return Status::Unsupported;
				}
				value = value_obj->get_unsafe<uint64_t>();
			}

			value >>= bit_offset;

			uint32_t size_mask = (uint32_t {1} << bits) - 1;
			value &= size_mask;

			dest_value |= value << i;
			i += bits;
			byte_offset += field->access_size;
		}

		dest->data = dest_value;
	}

	return Status::Success;
}

Status Interpreter::write_field(Field* field, const ObjectRef& value) {
	if (field->bit_size > 64) {
		LOG << "qacpi error: Field sizes greater than 8 bytes are not supported" << endlog;
		return Status::Unsupported;
	}
	else {
		auto int_value = value->get_unsafe<uint64_t>();

		uint32_t byte_offset = (field->bit_offset & ~((field->access_size * 8) - 1)) / 8;
		for (uint32_t i = 0; i < field->bit_size;) {
			uint32_t bit_offset = (field->bit_offset + i) & ((field->access_size * 8) - 1);
			uint32_t bits = QACPI_MIN(field->bit_size - i, (field->access_size * 8) - bit_offset);

			uint64_t old_value = 0;
			if (field->update == FieldUpdate::Preserve) {
				if (field->type == Field::Normal || field->type == Field::Bank) {
					if (field->type == Field::Bank) {
						ObjectRef bank_value;
						if (!bank_value) {
							return Status::NoMemory;
						}
						bank_value->data = field->bank_value;
						if (auto status = write_field(&field->data_bank->get_unsafe<Field>(), bank_value);
							status != Status::Success) {
							return status;
						}
					}

					auto& region = field->owner_index->get_unsafe<OpRegion>();
					if (auto status = region.read(byte_offset, field->access_size, old_value);
						status != Status::Success) {
						return status;
					}
				}
				else {
					auto& index = field->owner_index->get_unsafe<Field>();
					auto& data = field->data_bank->get_unsafe<Field>();

					ObjectRef offset;
					if (!offset) {
						return Status::NoMemory;
					}
					offset->data = uint64_t {byte_offset};
					if (auto status = write_field(&index, offset);
						status != Status::Success) {
						return status;
					}

					ObjectRef value_obj;
					if (!value_obj) {
						return Status::NoMemory;
					}

					if (auto status = read_field(&data, value_obj);
						status != Status::Success) {
						return status;
					}
					if (!value_obj->get<uint64_t>()) {
						LOG << "qacpi error: IndexField Data field with size greater than 8 bytes is not supported" << endlog;
						return Status::Unsupported;
					}
					old_value = value_obj->get_unsafe<uint64_t>();
				}
			}
			else if (field->update == FieldUpdate::WriteAsOnes) {
				old_value = 0xFFFFFFFFFFFFFFFF;
			}
			else {
				old_value = 0;
			}

			uint32_t size_mask = (uint32_t {1} << bits) - 1;

			uint64_t new_value = old_value;
			new_value &= ~(size_mask << bit_offset);
			new_value |= ((int_value >> i) & size_mask) << bit_offset;

			if (field->type == Field::Normal || field->type == Field::Bank) {
				if (field->type == Field::Bank) {
					ObjectRef bank_value;
					if (!bank_value) {
						return Status::NoMemory;
					}
					bank_value->data = field->bank_value;
					if (auto status = write_field(&field->data_bank->get_unsafe<Field>(), bank_value);
						status != Status::Success) {
						return status;
					}
				}

				auto& region = field->owner_index->get_unsafe<OpRegion>();
				if (auto status = region.write(byte_offset, field->access_size, new_value);
					status != Status::Success) {
					return status;
				}
			}
			else {
				auto& index = field->owner_index->get_unsafe<Field>();
				auto& data = field->data_bank->get_unsafe<Field>();

				ObjectRef offset;
				if (!offset) {
					return Status::NoMemory;
				}
				offset->data = uint64_t {byte_offset};
				if (auto status = write_field(&index, offset);
					status != Status::Success) {
					return status;
				}

				ObjectRef value_obj;
				if (!value_obj) {
					return Status::NoMemory;
				}
				value_obj->data = new_value;

				if (auto status = write_field(&data, value_obj);
					status != Status::Success) {
					return status;
				}
			}

			i += bits;
			byte_offset += field->access_size;
		}
	}

	return Status::Success;
}

Interpreter::MethodFrame::MethodFrame(Interpreter::MethodFrame&& other) noexcept {
	for (int i = 0; i < 7; ++i) {
		args[i] = move(other.args[i]);
	}
	for (int i = 0; i < 8; ++i) {
		locals[i] = move(other.locals[i]);
	}
	node_link = other.node_link;
	mutex_link = other.mutex_link;
	serialize_mutex = move(other.serialize_mutex);
	other.moved = true;
}

Interpreter::MethodFrame::~MethodFrame() {
	if (!moved) {
		if (serialize_mutex && serialize_mutex->handle) {
			if (serialize_mutex->recursion) {
				--serialize_mutex->recursion;
			}
			else {
				serialize_mutex->unlock();
			}
		}

		Mutex* mutex = mutex_link;
		while (mutex) {
			LOG << "qacpi warning: some mutexes were not unlocked at the end of a method scope" << endlog;
			mutex->unlock();
			mutex = mutex->next;
		}

		NamespaceNode* node = node_link;
		while (node) {
			auto* next = node->link;
			node->~NamespaceNode();
			qacpi_os_free(node, sizeof(NamespaceNode));
			node = next;
		}
	}
}
