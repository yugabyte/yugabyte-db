#pragma once

#include <duckdb/common/error_data.hpp>

extern "C" {
#include "postgres.h"
}

namespace pgduckdb {

template <typename Func, Func func, typename... FuncArgs>
typename std::invoke_result<Func, FuncArgs &...>::type
__CPPFunctionGuard__(const char *func_name, const char *file_name, int line, FuncArgs &...args) {
	const char *error_message = nullptr;
	auto pg_es_start = PG_exception_stack;
	try {
		return func(args...);
	} catch (duckdb::Exception &ex) {
		duckdb::ErrorData edata(ex.what());
		error_message = pstrdup(edata.Message().c_str());
	} catch (std::exception &ex) {
		const auto msg = ex.what();
		if (msg[0] == '{') {
			duckdb::ErrorData edata(ex.what());
			error_message = pstrdup(edata.Message().c_str());
		} else {
			error_message = pstrdup(ex.what());
		}
	}

	// This can happen if `func` or one of the function it calls uses `PG_TRY`
	// If we are here, it means we got out of the PG_TRY block through a C++ exception
	// without running the corresponding `PG_CATCH` block.
	//
	// In that case, the `PG_exception_stack` points to an invalid stack and calling
	// `elog(ERROR, ...)` below would lead to a crash when we finally hit `PG_CATCH`
	//
	// As a best effort, we reset the `PG_exception_stack` to the value we had
	// entering the wrapper, but this might leak other resources that were supposed
	// to be closed in the missed `PG_CATCH`
	//
	// For developers trying to fix this:
	// you need to make sure that `func` can never throw a C++ excepction
	// from within a `PG_TRY` block.

	// Example of problematic code:
	// void my_func() {
	//   PG_TRY(); {
	//     throw duckdb::Exception("foo");
	//   } PG_CATCH(); {
	//    // This PG_CATCH block will never be executed
	//   } PG_END_TRY();
	// }

	// In this case the `PG_CATCH` block that will handle the error thrown below
	// would try to reset the stack to the beginning of `my_func` and crash
	//
	// So instead this should also be wrapped in a `InvokeCPPFunc` like:
	//
	// void my_throwing_func() {
	//  throw duckdb::Exception("foo");
	// }
	//
	// void my_func() {
	//   PG_TRY(); {
	//     InvokeCPPFunc(my_throwing_func);
	//   } PG_CATCH(); {
	//    // This PG_CATCH block will now be executed
	//   } PG_END_TRY();
	// }
	if (pg_es_start != PG_exception_stack) {
		elog(WARNING, "WARNING: Unexpected exception stack pointer. This is not expected, please report this.");
		PG_exception_stack = pg_es_start;
	}

	// Simplified version of `elog(ERROR, ...)`, with arguments inlined
	if (errstart_cold(ERROR, TEXTDOMAIN)) {
		errmsg_internal("(PGDuckDB/%s) %s", func_name, error_message);
		errfinish(file_name, line, func_name);
	}
	pg_unreachable();
}

} // namespace pgduckdb

#define InvokeCPPFunc(FUNC, ...)                                                                                       \
	pgduckdb::__CPPFunctionGuard__<decltype(&FUNC), &FUNC>(#FUNC, __FILE__, __LINE__, ##__VA_ARGS__)

// Wrappers

#define DECLARE_PG_FUNCTION(func_name)                                                                                 \
	PG_FUNCTION_INFO_V1(func_name);                                                                                    \
	Datum func_name##_cpp(PG_FUNCTION_ARGS);                                                                           \
	Datum func_name(PG_FUNCTION_ARGS) {                                                                                \
		return InvokeCPPFunc(func_name##_cpp, fcinfo);                                                                 \
	}                                                                                                                  \
	Datum func_name##_cpp(PG_FUNCTION_ARGS __attribute__((unused)))
