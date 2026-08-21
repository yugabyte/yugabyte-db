#include "pgduckdb/pg/guc.hpp"
#include "pgduckdb/pgduckdb_utils.hpp"

extern "C" {
#include "postgres.h"
#include "utils/guc.h"
}

namespace pgduckdb::pg {

const char *
GetConfigOption(const char *name, bool missing_ok, bool restrict_privileged) {
	return PostgresFunctionGuard(::GetConfigOption, name, missing_ok, restrict_privileged);
}
} // namespace pgduckdb::pg
