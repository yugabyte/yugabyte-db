/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef AG_CYPHER_PARSE_NODE_H
#define AG_CYPHER_PARSE_NODE_H

#include "nodes/cypher_nodes.h"

/*
 * Every internal alias or variable name should be prefixed
 * with AGE_DEFAULT_PREFIX. Grammer restricts variables
 * prefixed with _age_default_ in user query to be used.
 */
#define AGE_DEFAULT_PREFIX "_age_default_"
#define AGE_DEFAULT_ALIAS_PREFIX AGE_DEFAULT_PREFIX"alias_"
#define AGE_DEFAULT_VARNAME_PREFIX AGE_DEFAULT_PREFIX"varname_"

typedef struct cypher_parsestate
{
    ParseState pstate;
    char *graph_name;
    uint32 graph_oid;
    Param *params;
    int default_alias_num;
    List *entities;
    List *property_constraint_quals;
    bool subquery_where_flag; /* flag for knowing we are in a subquery where */
    /*
     * To flag when an aggregate has been found in an expression during an
     * expression transform. This is used during the return_item list transform
     * to know which expressions are group by keys (not an aggregate or a
     * composite expression with an aggregate), and which aren't (everything
     * else). It is only used by transform_cypher_item_list.
     */
    bool exprHasAgg;
    bool p_opt_match;
    bool p_list_comp;
} cypher_parsestate;

typedef struct errpos_ecb_state
{
    ErrorContextCallback ecb;
    ParseState *pstate; /* ParseState of query that has subquery being parsed */
    int query_loc; /* location of subquery starting from p_sourcetext */
} errpos_ecb_state;

cypher_parsestate *make_cypher_parsestate(cypher_parsestate *parent_cpstate);
void free_cypher_parsestate(cypher_parsestate *cpstate);
#define get_parse_state(cpstate) ((ParseState *)(cpstate))

void setup_errpos_ecb(errpos_ecb_state *ecb_state, ParseState *pstate,
                      int query_loc);
void cancel_errpos_ecb(errpos_ecb_state *ecb_state);
char *get_next_default_alias(cypher_parsestate *cpstate);

#endif
