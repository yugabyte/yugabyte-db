#pragma once

#include "pgduckdb/pg/declarations.hpp"

extern "C" {
namespace pgduckdb {

// The FDW validator doesn't provide the server type
// when validating `CREATE SERVER`, nor the server oid
// when validating `CREATE USER MAPPING`.
// So we need to store them here to be able to validate
// the options.
extern const char *CurrentServerType;
extern Oid CurrentServerOid;

Oid FindMotherDuckForeignServerOid();

/*

Returns the Oid of the USER MAPPING for the given user and server.
Returns InvalidOid if no such mapping exists.

Note: we cannot use PG's `GetUserMapping` because:
- it checks the global scope if not found
- it throws an error if no mapping is not found

*/
Oid FindUserMappingOid(Oid user_oid, Oid server_oid);

/*

Returns the Oid of the `tables_owner_role` option defined in the `motherduck` SERVER
if it exists, returns the SERVER's owner otherwise.

*/
Oid GetMotherDuckPostgresRoleOid(Oid server_oid);

/*

Return the `default_database` setting defined in the `motherduck` SERVER
if it exists, returns nullptr otherwise.

*/
const char *FindMotherDuckDefaultDatabase();

/*

- if `pgduckdb::is_background_worker` then:
  > returns `token` option defined in the USER MAPPING of the owner of the SERVER if it exists
  > returns nullptr otherwise
- if not `pgduckdb::is_background_worker` then:
  > returns `token` option defined in the USER MAPPING of the current user if it exists
  > returns nullptr otherwise

*/
const char *FindMotherDuckToken();

const char *FindMotherDuckBackgroundCatalogRefreshInactivityTimeout();

void RecordDependencyOnMDServer(ObjectAddress *);

} // namespace pgduckdb
}
