#pragma once

namespace qacpi {
	enum class Status {
		Success,
		UnexpectedEof,
		InvalidAml,
		InvalidArgs,
		InvalidType,
		NoMemory,
		NotFound,
		MethodNotFound,
		TimeOut,
		Unsupported,
		InternalError,
		EndOfResources,
		InvalidResource
	};
}
