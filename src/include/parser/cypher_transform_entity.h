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

#ifndef AG_CYPHER_TRANSFORM_ENTITY_H
#define AG_CYPHER_TRANSFORM_ENTITY_H

#include "nodes/primnodes.h"
#include "parser/parse_node.h"

#include "nodes/cypher_nodes.h"
#include "nodes/makefuncs.h"
#include "parser/cypher_parse_node.h"

enum transform_entity_type
{
    ENT_VERTEX = 0x0,
    ENT_EDGE,
    ENT_VLE_EDGE,
    ENT_PATH
};

enum transform_entity_join_side
{
    JOIN_SIDE_LEFT = 0x0,
    JOIN_SIDE_RIGHT
};

/*
 * In the transformation stage, we need to track
 * where a variable came from. When moving between
 * clauses, Postgres parsestate and Query data structures
 * are insufficient for some of the information we
 * need.
 */
typedef struct
{
    // denotes whether this entity is a vertex or edge
    enum transform_entity_type type;

    /*
     * MATCH clauses are transformed into a select * FROM ... JOIN, etc
     * We need to know whether the table that this entity represents is
     * part of the join tree. If a cypher_node does not meet the conditions
     * set in INCLUDE_NODE_IN_JOIN_TREE. Then we can skip the node when
     * constructing our join tree. The entities around this particular entity
     * need to know this for the join to get properly constructed.
     */
    bool in_join_tree;

    /*
     * The parse data structure will be transformed into an Expr that represents
     * the entity. When constructing the join tree, we need to know what it was
     * turned into. If the entity was originally created in a previous clause,
     * this will be a Var that we need to reference to extract the id, startid,
     * endid for the join. If the entity was created in the current clause, then
     * this will be a FuncExpr that we can reference to get the id, startid, and
     * endid.
     */
    Expr *expr;

    /*
     * tells each clause whether this variable was
     * declared by itself or a previous clause.
     */
    bool declared_in_current_clause;
    // The parse data structure that we transformed
    union
    {
        cypher_node *node;
        cypher_relationship *rel;
        cypher_path *path;
    } entity;
} transform_entity;

transform_entity *find_variable(cypher_parsestate *cpstate, char *name);
transform_entity *find_transform_entity(cypher_parsestate *cpstate,
                                        char *name,
                                        enum transform_entity_type type);
transform_entity *make_transform_entity(cypher_parsestate *cpstate,
                                        enum transform_entity_type type,
                                        Node *node, Expr *expr);
char *get_entity_name(transform_entity *entity);
Expr *get_relative_expr(transform_entity *entity, Index levelsup);

#endif
