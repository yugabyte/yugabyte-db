#pragma once

#include "pgduckdb/pg/declarations.hpp"

namespace pgduckdb::pg {
extern const char *GetConfigOption(const char *name, bool missing_ok = false, bool restrict_privileged = true);
}
