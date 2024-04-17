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
				While
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
			ObjectRef args[8] {
				ObjectRef::empty(), ObjectRef::empty(), ObjectRef::empty(),
				ObjectRef::empty(), ObjectRef::empty(), ObjectRef::empty(),
				ObjectRef::empty(), ObjectRef::empty()};
			ObjectRef locals[7] {
				ObjectRef::empty(), ObjectRef::empty(), ObjectRef::empty(),
				ObjectRef::empty(), ObjectRef::empty(), ObjectRef::empty(),
				ObjectRef::empty()
			};
			bool moved {};
		};

		enum class SearchFlags {
			Create,
			Search
		};

		NamespaceNode* create_or_get_node(StringView name, SearchFlags flags);

		Status execute(const uint8_t* aml, uint32_t size);
		Status invoke_method(NamespaceNode* node, ObjectRef& res, ObjectRef* args, int arg_count);
		Status resolve_object(ObjectRef& object);
		Status handle_name(Frame& frame, bool need_result, bool super_name);
		Status try_convert(ObjectRef& object, ObjectRef& res, const ObjectType* types, int type_count);

		Status read_field(Field* field, uint64_t& res);
		Status write_field(Field* field, uint64_t value);

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

		Status parse_field_list(
			Frame& frame,
			Variant<NormalFieldInfo, IndexFieldInfo, BankFieldInfo> owner,
			uint32_t len,
			uint8_t flags);

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
		SmallVec<Variant<PkgLength, ObjectRef, String, MethodArgs>, 8> objects {};

		static Status parse_pkg_len(Frame& frame, PkgLength& res);

		Mutex* global_locked_mutexes {};
	};
}
