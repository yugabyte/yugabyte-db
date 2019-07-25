#ifndef AG_CYPHER_CLAUSE_H
#define AG_CYPHER_CLAUSE_H

#include "postgres.h"

#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"
#include "parser/parse_node.h"

Query *transform_cypher_stmt(ParseState *pstate, List *stmt);

#endif
