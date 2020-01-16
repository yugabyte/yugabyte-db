#ifndef AG_CYPHER_SCAN_STATE_H
#define AG_CYPHER_SCAN_STATE_H

#include "executor/tuptable.h"
#include "nodes/extensible.h"
#include "nodes/relation.h"

typedef struct cypher_create_scan_state
{
    CustomScanState css;
    List *pattern;
} cypher_create_scan_state;

extern const CustomExecMethods cypher_create_custom_exec_methods;

#endif
