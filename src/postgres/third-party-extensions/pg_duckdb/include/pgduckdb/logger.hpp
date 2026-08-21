#pragma once

#include "pgduckdb/pgduckdb_process_lock.hpp"

#include "pgduckdb/utility/cpp_only_file.hpp" // Must be last include.

extern "C" {
bool errstart(int elevel, const char *domain);
void errfinish(const char *filename, int lineno, const char *funcname);
int errmsg_internal(const char *fmt, ...);
bool message_level_is_interesting(int elevel);
}

namespace pgduckdb {

/* PG Error level codes */
#define DEBUG5              10
#define DEBUG4              11
#define DEBUG3              12
#define DEBUG2              13
#define DEBUG1              14
#define LOG                 15
#define INFO                17
#define NOTICE              18
#define WARNING             19
#define PGWARNING           19
#define WARNING_CLIENT_ONLY 20

// From PG elog.h
#ifdef __GNUC__
#define pg_attribute_unused() __attribute__((unused))
#else
#define pg_attribute_unused()
#endif

#if defined(errno) && defined(__linux__)
#define pd_prevent_errno_in_scope() int __errno_location pg_attribute_unused()
#elif defined(errno) && (defined(__darwin__) || defined(__FreeBSD__))
#define pd_prevent_errno_in_scope() int __error pg_attribute_unused()
#else
#define pd_prevent_errno_in_scope()
#endif

#define pd_ereport_domain(elevel, domain, ...)                                                                         \
	do {                                                                                                               \
		pd_prevent_errno_in_scope();                                                                                   \
		static_assert(elevel >= DEBUG5 && elevel <= WARNING_CLIENT_ONLY, "Invalid error level");                       \
		if (message_level_is_interesting(elevel)) {                                                                    \
			std::lock_guard<std::recursive_mutex> __pd_log_lock(pgduckdb::GlobalProcessLock::GetLock());               \
			if (errstart(elevel, domain))                                                                              \
				__VA_ARGS__, errfinish(__FILE__, __LINE__, __func__);                                                  \
		}                                                                                                              \
	} while (0)

#define TEXTDOMAIN NULL

#define pd_ereport(elevel, ...) pd_ereport_domain(elevel, TEXTDOMAIN, __VA_ARGS__)

#define pd_log(elevel, ...) pd_ereport(elevel, errmsg_internal(__VA_ARGS__))

} // namespace pgduckdb
