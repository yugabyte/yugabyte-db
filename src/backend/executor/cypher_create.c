#include "postgres.h"

#include "executor/tuptable.h"
#include "nodes/execnodes.h"
#include "nodes/extensible.h"
#include "nodes/nodes.h"
#include "nodes/plannodes.h"

#include "executor/cypher_executor.h"

static void begin_cypher_create(CustomScanState *node, EState *estate,
                                int eflags);
static TupleTableSlot *exec_cypher_create(CustomScanState *node);
static void end_cypher_create(CustomScanState *node);

const CustomExecMethods cypher_create_exec_methods = {
    "Cypher Create",
    begin_cypher_create,
    exec_cypher_create,
    end_cypher_create,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

static void begin_cypher_create(CustomScanState *node, EState *estate,
                                int eflags)
{
}

static TupleTableSlot *exec_cypher_create(CustomScanState *node)
{
    return NULL;
}

static void end_cypher_create(CustomScanState *node)
{
}

Node *create_cypher_create_plan_state(CustomScan *cscan)
{
    CustomScanState *css;

    css = makeNode(CustomScanState);

    css->methods = &cypher_create_exec_methods;

    return (Node *)css;
}
