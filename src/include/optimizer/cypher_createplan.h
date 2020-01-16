#ifndef AG_CYPHER_CREATEPLAN_H
#define AG_CYPHER_CREATEPLAN_H

#include "nodes/pg_list.h"
#include "nodes/plannodes.h"
#include "nodes/relation.h"

Plan *plan_cypher_create_path(PlannerInfo *root, RelOptInfo *rel,
                              CustomPath *best_path, List *tlist,
                              List *clauses, List *custom_plans);

#endif
