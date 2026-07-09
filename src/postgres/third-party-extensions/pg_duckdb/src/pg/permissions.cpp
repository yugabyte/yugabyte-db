extern "C" {
#include "postgres.h"
#include "miscadmin.h"         // GetUserId
#include "catalog/pg_authid.h" // ROLE_PG_WRITE_SERVER_FILES, ROLE_PG_READ_SERVER_FILES
#include "utils/acl.h"         // is_member_of_role
/* YB includes */
#include "executor/executor.h" // ExecCheckRTPerms, ExecutorCheckPerms_hook
}

namespace pgduckdb::pg {

bool
AllowRawFileAccess() {
	return is_member_of_role(GetUserId(), ROLE_PG_WRITE_SERVER_FILES) &&
	       is_member_of_role(GetUserId(), ROLE_PG_READ_SERVER_FILES);
}

#if PG_VERSION_NUM < 160000
/*
 * YB: Run ExecCheckRTPerms() with ExecutorCheckPerms_hook temporarily cleared.
 * pg_duckdb calls this from utility/COPY and from planning (DuckdbPlanNode), where there is no
 * executor stack yet -> SIGSEGV in pgaudit.c.
 *
 * Behavior change: upstream pg_duckdb runs this hook on these paths, so clearing it is
 * not purely a crash workaround. For an operation routed through this path (e.g. COPY ... TO
 * 's3://...' via DuckDB):
 *   - pgaudit object auditing produces NO audit record for it (audit gap), and
 *   - any extension that enforces access by returning false / erroring from the hook is skipped.
 *
 * TODO(#32512): the durable fix is a NULL-auditEventStack guard in pgaudit itself; once
 * that lands, this helper can be deleted and all call sites can use plain ExecCheckRTPerms.
 */
void
YbExecCheckRTPerms(List *rtable) {
	ExecutorCheckPerms_hook_type saved_hook = ExecutorCheckPerms_hook;
	ExecutorCheckPerms_hook = NULL;
	PG_TRY();
	{
		ExecCheckRTPerms(rtable, true);
	}
	PG_FINALLY();
	{
		ExecutorCheckPerms_hook = saved_hook;
	}
	PG_END_TRY();
}
#endif

} // namespace pgduckdb::pg
