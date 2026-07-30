#include "pgduckdb/pg/error_data.hpp"

extern "C" {
#include "postgres.h"
}

namespace pgduckdb::pg {
const char *
GetErrorDataMessage(ErrorData *error_data) {
	return error_data->message;
}
} // namespace pgduckdb::pg
