#ifndef AG_CYPHER_PATH_H
#define AG_CYPHER_PATH_H

#include "nodes/extensible.h"
#include "nodes/relation.h"

struct Plan *plan_cypher_create_path(PlannerInfo *root, RelOptInfo *rel,
                                     struct CustomPath *best_path, List *tlist,
                                     List *clauses, List *custom_plans);

void set_rel_pathlist_init(void);
void set_rel_pathlist_fini(void);

#endif
