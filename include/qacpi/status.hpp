#pragma once

namespace qacpi {
	enum class Status {
		Success,
		UnexpectedEof,
		InvalidAml,
		InvalidArgs,
		NoMemory,
		NotFound,
		MethodNotFound,
		TimeOut,
		Unsupported,
		InternalError
	};
}
