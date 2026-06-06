#pragma once

extern "C" {
struct ErrorData;
}

namespace pgduckdb::pg {
const char *GetErrorDataMessage(ErrorData *error_data);
}
