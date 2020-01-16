#ifndef AG_CYPHER_PATHNODE_H
#define AG_CYPHER_PATHNODE_H

#include "nodes/pg_list.h"
#include "nodes/relation.h"

CustomPath *create_cypher_create_path(PlannerInfo *root, RelOptInfo *rel,
                                      List *custom_private);

#endif
