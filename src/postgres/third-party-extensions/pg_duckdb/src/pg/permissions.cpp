extern "C" {
#include "postgres.h"
#include "miscadmin.h"         // GetUserId
#include "catalog/pg_authid.h" // ROLE_PG_WRITE_SERVER_FILES, ROLE_PG_READ_SERVER_FILES
#include "utils/acl.h"         // is_member_of_role
}

namespace pgduckdb::pg {

bool
AllowRawFileAccess() {
	return is_member_of_role(GetUserId(), ROLE_PG_WRITE_SERVER_FILES) &&
	       is_member_of_role(GetUserId(), ROLE_PG_READ_SERVER_FILES);
}

} // namespace pgduckdb::pg
