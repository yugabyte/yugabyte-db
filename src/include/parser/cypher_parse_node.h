/*
 * Copyright 2020 Bitnine Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AG_CYPHER_PARSE_NODE_H
#define AG_CYPHER_PARSE_NODE_H

#include "nodes/primnodes.h"
#include "parser/parse_node.h"

#define AG_DEFAULT_ALIAS_BASE "_ag_default_alias_"

typedef struct cypher_parsestate
{
    ParseState pstate;
    char *graph_name;
    Oid graph_oid;
    Param *params;
    int default_alias_num;
    List *entities;
    List *property_constraint_quals;
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
RangeTblEntry *find_rte(cypher_parsestate *cpstate, char *varname);

#endif
