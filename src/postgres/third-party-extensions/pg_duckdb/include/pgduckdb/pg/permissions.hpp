/* YB includes */
#include "pgduckdb/pg/declarations.hpp"

namespace pgduckdb::pg {
bool AllowRawFileAccess();

/*
 * YbExecCheckRTPerms: YB's variant of the core ExecCheckRTPerms() RTE permission check. It runs the
 * check with ExecutorCheckPerms_hook temporarily cleared.
 */
void YbExecCheckRTPerms(List *rtable);
}
