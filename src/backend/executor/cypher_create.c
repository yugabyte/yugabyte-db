#include "postgres.h"

#include "optimizer/tlist.h"

#include "nodes/ag_nodes.h"
#include "nodes/cypher_execnodes.h"

void cypher_create_begin_custom_scan(CustomScanState *node, EState *estate,
                                     int eflags);
TupleTableSlot *cypher_create_exec_custom_scan(CustomScanState *node);
void cypher_create_end_custom_scan(CustomScanState *node);

const CustomExecMethods cypher_create_custom_exec_methods = {
    "Cypher Create Custom Exec Methods",
    cypher_create_begin_custom_scan,
    cypher_create_exec_custom_scan,
    cypher_create_end_custom_scan,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL};

void cypher_create_begin_custom_scan(CustomScanState *node, EState *estate,
                                     int eflags)
{
}

TupleTableSlot *cypher_create_exec_custom_scan(CustomScanState *node)
{
    return NULL;
}

void cypher_create_end_custom_scan(CustomScanState *node)
{
}
