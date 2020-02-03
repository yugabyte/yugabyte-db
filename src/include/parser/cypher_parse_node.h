#ifndef AG_CYPHER_PARSE_NODE_H
#define AG_CYPHER_PARSE_NODE_H

#include "nodes/primnodes.h"
#include "parser/parse_node.h"

typedef struct cypher_parsestate
{
    ParseState pstate;
    char *graph_name;
    Oid graph_oid;
    Param *params;
} cypher_parsestate;

typedef struct errpos_ecb_state
{
    ErrorContextCallback ecb;
    ParseState *pstate; // ParseState of query that has subquery being parsed
    int query_loc; // location of subquery starting from p_sourcetext
} errpos_ecb_state;

cypher_parsestate *make_cypher_parsestate(cypher_parsestate *parent_cpstate);
void free_cypher_parsestate(cypher_parsestate *cpstate);
#define get_parse_state(cpstate) ((ParseState *)(cpstate))

void setup_errpos_ecb(errpos_ecb_state *ecb_state, ParseState *pstate,
                      int query_loc);
void cancel_errpos_ecb(errpos_ecb_state *ecb_state);

#endif
