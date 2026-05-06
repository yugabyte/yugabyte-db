#pragma once

#include "string"
#include "pgduckdb/pg/declarations.hpp"

namespace pgduckdb::pg {

std::string GetArgString(FunctionCallInfo info, int argno);
Datum GetArgDatum(FunctionCallInfo info, int argno);
std::string DatumToString(Datum datum);

} // namespace pgduckdb::pg
