#ifndef AG_CYPHER_CLAUSE_H
#define AG_CYPHER_CLAUSE_H

#include "postgres.h"

#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"
#include "parser/parse_node.h"

#include "parser/cypher_parse_node.h"

typedef struct cypher_clause cypher_clause;

struct cypher_clause
{
    Node *self;
    cypher_clause *prev; // previous clause
};

Query *transform_cypher_clause(cypher_parsestate *cpstate,
                               cypher_clause *clause);

#endif
