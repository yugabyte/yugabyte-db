#pragma once

#include "pgduckdb/pg/declarations.hpp"

struct UserMapping;

namespace pgduckdb {
namespace pg {

List *ListDuckDBCreateSecretQueries();

/*
Validate a SECRET defined by: 1. a SERVER (TYPE and OPTIONS) and optionally 2. a USER MAPPING.
We validate by creating the query to create the SECRET in DuckDB and then executing it.
This function throws a PG error if the secret is invalid.
*/
void ValidateDuckDBSecret(const char *type, List *server_options, List *mapping_options = nullptr);

/* Utility functions */
UserMapping *FindUserMapping(Oid userid, Oid serverid, bool with_options = false);

const char *FindOption(List *options, const char *name);
const char *GetOption(List *options, const char *name);
} // namespace pg
} // namespace pgduckdb
