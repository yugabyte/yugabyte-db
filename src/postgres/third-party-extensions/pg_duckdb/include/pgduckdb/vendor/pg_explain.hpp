#pragma once
extern "C" {
#include "postgres.h"

#include "commands/explain.h"

#if PG_VERSION_NUM < 170000
void standard_ExplainOneQuery(Query *query, int cursorOptions, IntoClause *into, ExplainState *es,
                              const char *queryString, ParamListInfo params, QueryEnvironment *queryEnv);
#endif
}
