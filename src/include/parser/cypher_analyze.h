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

#ifndef AG_CYPHER_ANALYZE_H
#define AG_CYPHER_ANALYZE_H

#include "parser/cypher_clause.h"

typedef bool (*cypher_expression_condition)( Node *expr);

void post_parse_analyze_init(void);
void post_parse_analyze_fini(void);

cypher_clause *build_subquery_node(cypher_clause *next);

/*expr tree walker */
bool expr_contains_node(cypher_expression_condition is_expr, Node *expr);
bool expr_has_subquery(Node * expr);

#endif
