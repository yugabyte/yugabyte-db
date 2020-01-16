#ifndef AG_CYPHER_EXECUTOR_H
#define AG_CYPHER_EXECUTOR_H

#include "nodes/extensible.h"
#include "nodes/nodes.h"
#include "nodes/plannodes.h"

Node *create_cypher_create_plan_state(CustomScan *cscan);
extern const CustomExecMethods cypher_create_exec_methods;

#endif
