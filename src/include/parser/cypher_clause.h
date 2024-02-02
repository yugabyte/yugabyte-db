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

#ifndef AG_CYPHER_CLAUSE_H
#define AG_CYPHER_CLAUSE_H

#include "parser/cypher_parse_node.h"

typedef struct cypher_clause cypher_clause;

struct cypher_clause
{
    cypher_clause *next; //next clause
    Node *self;
    cypher_clause *prev; // previous clause
};

Query *transform_cypher_clause(cypher_parsestate *cpstate,
                               cypher_clause *clause);

Query *cypher_parse_sub_analyze(Node *parseTree,
                                cypher_parsestate *cpstate,
                                CommonTableExpr *parentCTE,
                                bool locked_from_parent,
                                bool resolve_unknowns);
#endif
