#include "ops.hpp"
#include "aml_ops.hpp"

namespace qacpi {
	constexpr Array<OpBlock, 0x100> OPS = [] {
		Array<OpBlock, 0x100> res {};

		res[ZeroOp] = {1, {
			Op::CallHandler
		}, OpHandler::Constant};
		res[OneOp] = {1, {
			Op::CallHandler
		}, OpHandler::Constant};
		res[AliasOp] = {3, {
			Op::NameString,
			Op::NameString,
			Op::CallHandler
		}, OpHandler::Alias};
		res[NameOp] = {3, {
			Op::NameString,
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::Name};
		res[BytePrefix] = {1, {
			Op::CallHandler
		}, OpHandler::Constant};
		res[WordPrefix] = {1, {
			Op::CallHandler
		}, OpHandler::Constant};
		res[DWordPrefix] = {1, {
			Op::CallHandler
		}, OpHandler::Constant};
		res[StringPrefix] = {1, {
			Op::CallHandler
		}, OpHandler::String};
		res[QWordPrefix] = {1, {
			Op::CallHandler
		}, OpHandler::Constant};
		res[ScopeOp] = {3, {
			Op::PkgLength,
			Op::NameString,
			Op::CallHandler
		}, OpHandler::Scope};
		res[BufferOp] = {3, {
			Op::PkgLength,
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::Buffer};
		res[PackageOp] = {4, {
			Op::PkgLength,
			Op::Byte,
			Op::PkgElements,
			Op::CallHandler
		}, OpHandler::Package};
		res[VarPackageOp] = {4, {
			Op::PkgLength,
			Op::TermArg,
			Op::VarPkgElements,
			Op::CallHandler
		}, OpHandler::Package};
		res[MethodOp] = {4, {
			Op::PkgLength,
			Op::NameString,
			Op::Byte,
			Op::CallHandler
		}, OpHandler::Method};
		res[ExternalOp] = {4, {
			Op::NameString,
			Op::Byte,
			Op::Byte,
			Op::CallHandler
		}, OpHandler::External};
		res[Local0Op] = {1, {
			Op::CallHandler
		}, OpHandler::Local};
		res[Local1Op] = {1, {
			Op::CallHandler
		}, OpHandler::Local};
		res[Local2Op] = {1, {
			Op::CallHandler
		}, OpHandler::Local};
		res[Local3Op] = {1, {
			Op::CallHandler
		}, OpHandler::Local};
		res[Local4Op] = {1, {
			Op::CallHandler
		}, OpHandler::Local};
		res[Local5Op] = {1, {
			Op::CallHandler
		}, OpHandler::Local};
		res[Local6Op] = {1, {
			Op::CallHandler
		}, OpHandler::Local};
		res[Local7Op] = {1, {
			Op::CallHandler
		}, OpHandler::Local};
		res[Arg0Op] = {1, {
			Op::CallHandler
		}, OpHandler::Arg};
		res[Arg1Op] = {1, {
			Op::CallHandler
		}, OpHandler::Arg};
		res[Arg2Op] = {1, {
			Op::CallHandler
		}, OpHandler::Arg};
		res[Arg3Op] = {1, {
			Op::CallHandler
		}, OpHandler::Arg};
		res[Arg4Op] = {1, {
			Op::CallHandler
		}, OpHandler::Arg};
		res[Arg5Op] = {1, {
			Op::CallHandler
		}, OpHandler::Arg};
		res[Arg6Op] = {1, {
			Op::CallHandler
		}, OpHandler::Arg};
		res[StoreOp] = {3, {
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Store};
		res[RefOf] = {2, {
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::RefOf};
		res[AddOp] = {4, {
			Op::TermArg,
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Add};
		res[ConcatOp] = {4, {
			Op::TermArg,
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Concat};
		res[SubtractOp] = {4, {
			Op::TermArg,
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Subtract};
		res[IncrementOp] = {2, {
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Increment};
		res[DecrementOp] = {2, {
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Decrement};
		res[MultiplyOp] = {4, {
			Op::TermArg,
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Multiply};
		res[DivideOp] = {5, {
			Op::TermArg,
			Op::TermArg,
			Op::SuperName,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Divide};
		res[ShiftLeftOp] = {4, {
			Op::TermArg,
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Shl};
		res[ShiftRightOp] = {4, {
			Op::TermArg,
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Shr};
		res[AndOp] = {4, {
			Op::TermArg,
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::And};
		res[NandOp] = {4, {
			Op::TermArg,
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Nand};
		res[OrOp] = {4, {
			Op::TermArg,
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Or};
		res[NorOp] = {4, {
			Op::TermArg,
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Nor};
		res[XorOp] = {4, {
			Op::TermArg,
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Xor};
		res[NotOp] = {3, {
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Not};
		res[FindSetLeftBitOp] = {3, {
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::FindSetLeftBit};
		res[FindSetRightBitOp] = {3, {
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::FindSetRightBit};
		res[DerefOfOp] = {2, {
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::DerefOf};
		res[ModOp] = {4, {
			Op::TermArg,
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Mod};
		res[NotifyOp] = {3, {
			Op::SuperName,
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::Notify};
		res[SizeOfOp] = {2, {
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::SizeOf};
		res[IndexOp] = {4, {
			Op::TermArg,
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Index};
		res[MatchOp] = {7, {
			Op::TermArg,
			Op::Byte,
			Op::TermArg,
			Op::Byte,
			Op::TermArg,
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::Match};
		res[CreateDWordFieldOp] = {4, {
			Op::TermArg,
			Op::TermArg,
			Op::NameString,
			Op::CallHandler
		}, OpHandler::CreateDWordField};
		res[CreateWordFieldOp] = {4, {
			Op::TermArg,
			Op::TermArg,
			Op::NameString,
			Op::CallHandler
		}, OpHandler::CreateWordField};
		res[CreateByteFieldOp] = {4, {
			Op::TermArg,
			Op::TermArg,
			Op::NameString,
			Op::CallHandler
		}, OpHandler::CreateByteField};
		res[CreateBitFieldOp] = {4, {
			Op::TermArg,
			Op::TermArg,
			Op::NameString,
			Op::CallHandler
		}, OpHandler::CreateBitField};
		res[ObjectTypeOp] = {2, {
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::ObjectType};
		res[CreateQWordFieldOp] = {4, {
			Op::TermArg,
			Op::TermArg,
			Op::NameString,
			Op::CallHandler
		}, OpHandler::CreateQWordField};
		res[LAndOp] = {3, {
			Op::TermArg,
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::LAnd};
		res[LOrOp] = {3, {
			Op::TermArg,
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::LOr};
		res[LNotOp] = {2, {
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::LNot};
		res[LEqualOp] = {3, {
			Op::TermArg,
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::LEqual};
		res[LGreaterOp] = {3, {
			Op::TermArg,
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::LGreater};
		res[LLessOp] = {3, {
			Op::TermArg,
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::LLess};
		res[ToBufferOp] = {3, {
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::ToBuffer};
		res[ToDecimalStringOp] = {3, {
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::ToDecimalString};
		res[ToHexStringOp] = {3, {
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::ToHexString};
		res[ToIntegerOp] = {3, {
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::ToInteger};
		res[CopyObjectOp] = {3, {
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::CopyObject};
		res[ContinueOp] = {1, {
			Op::CallHandler
		}, OpHandler::Continue};
		res[IfOp] = {3, {
			Op::PkgLength,
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::If};
		res[ElseOp] = {2, {
			Op::PkgLength,
			Op::CallHandler
		}, OpHandler::Else};
		res[WhileOp] = {3, {
			Op::PkgLength,
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::While};
		res[NoopOp] = {1, {
			Op::CallHandler
		}, OpHandler::Noop};
		res[ReturnOp] = {2, {
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::Return};
		res[BreakOp] = {1, {
			Op::CallHandler
		}, OpHandler::Break};
		res[BreakPointOp] = {1, {
			Op::CallHandler
		}, OpHandler::BreakPoint};
		res[OnesOp] = {1, {
			Op::CallHandler
		}, OpHandler::Constant};

		return res;
	}();

	constexpr Array<OpBlock, 0x100> EXT_OPS = [] {
		Array<OpBlock, 0x100> res {};

		res[MutexOp] = {3, {
			Op::NameString,
			Op::Byte,
			Op::CallHandler
		}, OpHandler::Mutex};
		res[EventOp] = {2, {
			Op::NameString,
			Op::CallHandler
		}, OpHandler::Event};
		res[CondRefOfOp] = {3, {
			Op::SuperNameUnresolved,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::CondRefOf};
		res[CreateFieldOp] = {5, {
			Op::TermArg,
			Op::TermArg,
			Op::TermArg,
			Op::NameString,
			Op::CallHandler
		}, OpHandler::CreateField};
		res[LoadOp] = {3, {
			Op::NameString,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Load};
		res[StallOp] = {2, {
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::Stall};
		res[SleepOp] = {2, {
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::Sleep};
		res[AcquireOp] = {3, {
			Op::SuperName,
			Op::Word,
			Op::CallHandler
		}, OpHandler::Acquire};
		res[SignalOp] = {2, {
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Signal};
		res[WaitOp] = {3, {
			Op::SuperName,
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::Wait};
		res[ResetOp] = {2, {
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Reset};
		res[ReleaseOp] = {2, {
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::Release};
		res[FromBcdOp] = {3, {
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::FromBcd};
		res[ToBcd] = {3, {
			Op::TermArg,
			Op::SuperName,
			Op::CallHandler
		}, OpHandler::ToBcd};
		res[RevisionOp] = {1, {
			Op::CallHandler
		}, OpHandler::Revision};
		res[DebugOp] = {1, {
			Op::CallHandler
		}, OpHandler::Debug};
		res[FatalOp] = {4, {
			Op::Byte,
			Op::DWord,
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::Fatal};
		res[TimerOp] = {1, {
			Op::CallHandler
		}, OpHandler::Timer};
		res[OpRegionOp] = {5, {
			Op::NameString,
			Op::Byte,
			Op::TermArg,
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::OpRegion};
		res[FieldOp] = {6, {
			Op::PkgLength,
			Op::NameString,
			Op::Byte,
			Op::StartFieldList,
			Op::FieldList,
			Op::CallHandler
		}, OpHandler::Field};
		res[DeviceOp] = {3, {
			Op::PkgLength,
			Op::NameString,
			Op::CallHandler
		}, OpHandler::Device};
		res[ProcessorOp] = {6, {
			Op::PkgLength,
			Op::NameString,
			Op::Byte,
			Op::DWord,
			Op::Byte,
			Op::CallHandler
		}, OpHandler::Processor};
		res[PowerResOp] = {5, {
			Op::PkgLength,
			Op::NameString,
			Op::Byte,
			Op::Word,
			Op::CallHandler
		}, OpHandler::PowerRes};
		res[ThermalZoneOp] = {3, {
			Op::PkgLength,
			Op::NameString,
			Op::CallHandler
		}, OpHandler::ThermalZone};
		res[IndexFieldOp] = {7, {
			Op::PkgLength,
			Op::NameString,
			Op::NameString,
			Op::Byte,
			Op::StartFieldList,
			Op::FieldList,
			Op::CallHandler
		}, OpHandler::IndexField};
		res[BankFieldOp] = {8, {
			Op::PkgLength,
			Op::NameString,
			Op::NameString,
			Op::TermArg,
			Op::Byte,
			Op::StartFieldList,
			Op::FieldList,
			Op::CallHandler
		}, OpHandler::BankField};
		res[DataRegionOp] = {5, {
			Op::NameString,
			Op::TermArg,
			Op::TermArg,
			Op::TermArg,
			Op::CallHandler
		}, OpHandler::DataRegion};

		return res;
	}();
}
