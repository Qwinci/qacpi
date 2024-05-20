#pragma once
#include "qacpi/object.hpp"
#include "qacpi/context.hpp"
#include "qacpi/small_vec.hpp"
#include "ops.hpp"

namespace qacpi {
	struct Interpreter {
		~Interpreter();

		Context* context;
		uint8_t int_size {};

		struct OpBlockCtx {
			const OpBlock* block;
			uint32_t objects_at_start;
			uint8_t ip;
			bool processed;
			bool need_result;
			bool as_ref;
		};

		struct Frame {
			const uint8_t* start;
			const uint8_t* end;
			const uint8_t* ptr;
			NamespaceNode* parent_scope;
			SmallVec<OpBlockCtx, 8> op_blocks;
			bool need_result;
			bool is_method;
			enum : uint8_t {
				Scope,
				Package,
				If,
				While,
				FieldList
			} type;
		};

		struct MethodFrame {
			constexpr MethodFrame() = default;
			~MethodFrame();

			constexpr MethodFrame(const MethodFrame&) = delete;
			constexpr MethodFrame& operator=(const MethodFrame&) = delete;
			constexpr MethodFrame& operator=(MethodFrame&&) = delete;
			MethodFrame(MethodFrame&& other) noexcept;

			NamespaceNode* node_link {};
			Mutex* mutex_link {};
			Mutex* serialize_mutex {};
			ObjectRef args[7] {
				ObjectRef::empty(), ObjectRef::empty(), ObjectRef::empty(),
				ObjectRef::empty(), ObjectRef::empty(), ObjectRef::empty(),
				ObjectRef::empty()};
			ObjectRef locals[8] {
				ObjectRef::empty(), ObjectRef::empty(), ObjectRef::empty(),
				ObjectRef::empty(), ObjectRef::empty(), ObjectRef::empty(),
				ObjectRef::empty(), ObjectRef::empty()
			};
			bool moved {};
		};

		NamespaceNode* create_or_get_node(StringView name, Context::SearchFlags flags);

		Status execute(const uint8_t* aml, uint32_t size);
		Status invoke_method(NamespaceNode* node, ObjectRef& res, ObjectRef* args, int arg_count);
		Status resolve_object(ObjectRef& object);
		Status handle_name(Frame& frame, bool need_result, bool super_name);
		Status try_convert(ObjectRef& object, ObjectRef& res, const ObjectType* types, int type_count);

		Status read_field(Field* field, ObjectRef& dest);
		Status write_field(Field* field, const ObjectRef& value);

		template<int N>
		Status try_convert(ObjectRef& object, ObjectRef& res, const ObjectType (&types)[N]) {
			return try_convert(object, res, types, N);
		}

		ObjectRef pop_and_unwrap_obj();

		Status store_to_target(ObjectRef target, ObjectRef value);
		static Status parse_name_str(Frame& frame, String& res);

		struct NormalFieldInfo {
			ObjectRef owner;
		};
		struct IndexFieldInfo {
			ObjectRef index;
			ObjectRef data;
		};
		struct BankFieldInfo {
			ObjectRef owner;
			ObjectRef bank;
			uint64_t selection;
		};

		struct FieldList {
			SmallVec<NamespaceNode*, 8> nodes;
			ObjectRef connection;
			uint32_t offset;
			Frame frame;
			decltype(Field::Normal) type;
			uint8_t flags;
			bool connect_field;
			bool connect_field_part2;
		};

		Status parse_field(FieldList& list, Frame& frame);

		Status handle_op(Frame& frame, const OpBlockCtx& block, bool need_result);
		Status parse();

		SmallVec<Frame, 8> frames {};
		SmallVec<MethodFrame, 8> method_frames {};
		NamespaceNode* current_scope {context->get_root()};

		struct PkgLength {
			const uint8_t* start;
			uint32_t len;
		};
		struct MethodArgs {
			Method* method;
			NamespaceNode* parent_scope;
			uint8_t remaining;
		};
		SmallVec<Variant<PkgLength, ObjectRef, String, MethodArgs, FieldList>, 8> objects {};

		static Status parse_pkg_len(Frame& frame, PkgLength& res);

		Mutex* global_locked_mutexes {};
	};
}
