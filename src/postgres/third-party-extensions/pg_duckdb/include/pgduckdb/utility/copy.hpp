#pragma once

extern "C" {
#include "postgres.h"
#include "nodes/plannodes.h"
}

const char *MakeDuckdbCopyQuery(PlannedStmt *pstmt, const char *query_string, struct QueryEnvironment *query_env);
