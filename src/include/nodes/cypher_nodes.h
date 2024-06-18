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

#ifndef AG_CYPHER_NODE_H
#define AG_CYPHER_NODE_H

#include "nodes/ag_nodes.h"

/* cypher sub patterns/queries */
typedef enum csp_kind
{
        CSP_EXISTS,
        CSP_SIZE,
        CSP_FINDPATH /* shortestpath, allshortestpaths, dijkstra */
} csp_kind;

typedef struct cypher_sub_pattern
{
        ExtensibleNode extensible;
        csp_kind kind;
        List *pattern;
} cypher_sub_pattern;

typedef struct cypher_sub_query
{
        ExtensibleNode extensible;
        csp_kind kind;
        List *query;
} cypher_sub_query;

/*
 * clauses
 */

typedef struct cypher_return
{
    ExtensibleNode extensible;
    bool distinct;
    List *items; /* a list of ResTarget's */
    List *order_by;
    Node *skip;
    Node *limit;

    bool all_or_distinct;
    SetOperation op;
    List *larg; /* lefthand argument of the unions */
    List *rarg; /*righthand argument of the unions */
} cypher_return;

typedef struct cypher_with
{
    ExtensibleNode extensible;
    bool distinct;
    bool subquery_intermediate; /* flag that denotes a subquery node */
    List *items; /* a list of ResTarget's */
    List *order_by;
    Node *skip;
    Node *limit;
    Node *where;
} cypher_with;

typedef struct cypher_match
{
    ExtensibleNode extensible;
    List *pattern; /* a list of cypher_paths */
    Node *where; /* optional WHERE subclause (expression) */
    bool optional; /* OPTIONAL MATCH */
} cypher_match;

typedef struct cypher_create
{
    ExtensibleNode extensible;
    List *pattern; /* a list of cypher_paths */
} cypher_create;

typedef struct cypher_set
{
    ExtensibleNode extensible;
    List *items; /* a list of cypher_set_items */
    bool is_remove; /* true if this is REMOVE clause */
    int location;
} cypher_set;

typedef struct cypher_set_item
{
    ExtensibleNode extensible;
    Node *prop; /* LHS */
    Node *expr; /* RHS */
    bool is_add; /* true if this is += */
    int location;
} cypher_set_item;

typedef struct cypher_delete
{
    ExtensibleNode extensible;
    bool detach; /* true if DETACH is specified */
    List *exprs; /* targets of this deletion */
    int location;
} cypher_delete;

typedef struct cypher_unwind
{
    ExtensibleNode extensible;
    ResTarget *target;

    /* for list comprehension */
    Node *where;
    Node *collect;
} cypher_unwind;

typedef struct cypher_merge
{
    ExtensibleNode extensible;
    Node *path;
} cypher_merge;

/*
 * pattern
 */

typedef struct cypher_path
{
    ExtensibleNode extensible;
    List *path; /* [ node ( , relationship , node , ... ) ] */
    char *var_name;
    char *parsed_var_name;
    int location;
} cypher_path;

/* ( name :label props ) */
typedef struct cypher_node
{
    ExtensibleNode extensible;
    char *name;
    char *parsed_name;
    char *label;
    char *parsed_label;
    bool use_equals;
    Node *props; /* map or parameter */
    int location;
} cypher_node;

typedef enum
{
    CYPHER_REL_DIR_NONE = 0,
    CYPHER_REL_DIR_LEFT = -1,
    CYPHER_REL_DIR_RIGHT = 1
} cypher_rel_dir;

/* -[ name :label props ]- */
typedef struct cypher_relationship
{
    ExtensibleNode extensible;
    char *name;
    char *parsed_name;
    char *label;
    char *parsed_label;
    bool use_equals;
    Node *props; /* map or parameter */
    Node *varlen; /* variable length relationships (A_Indices) */
    cypher_rel_dir dir;
    int location;
} cypher_relationship;

/*
 * expression
 */

typedef struct cypher_bool_const
{
    ExtensibleNode extensible;
    bool boolean;
    int location;
} cypher_bool_const;

typedef struct cypher_integer_const
{
    ExtensibleNode extensible;
    int64 integer;
    int location;
} cypher_integer_const;

typedef struct cypher_param
{
    ExtensibleNode extensible;
    char *name;
    int location;
} cypher_param;

typedef struct cypher_map
{
    ExtensibleNode extensible;
    List *keyvals;
    int location;
    bool keep_null; /* if false, keyvals with null value are removed */
} cypher_map;

typedef struct cypher_map_projection
{
    ExtensibleNode extensible;
    ColumnRef *map_var; /* must be a map, vertex or an edge */
    List *map_elements; /* list of cypher_map_projection_element */
    int location;
} cypher_map_projection;

typedef enum cypher_map_projection_element_type
{
    PROPERTY_SELECTOR = 0,  /* map_var { .key } */
    VARIABLE_SELECTOR,      /* map_var { value } */
    LITERAL_ENTRY,          /* map_var { key: value } */
    ALL_PROPERTIES_SELECTOR /* map_var { .* } */
} cypher_map_projection_element_type;

typedef struct cypher_map_projection_element
{
    ExtensibleNode extensible;
    cypher_map_projection_element_type type;

    /*
     * key and/or value can be null depending on the type
     *
     * For PROPERTY_SELECTOR, value is null.
     * For VARIABLE_SELECTOR, key is null, and value is a ColumnRef.
     * For LITERAL_ENTRY, none is null (value is an Expr).
     * For ALL_PROPERTIES_SELECTOR, both are null.
     */
    char *key;
    Node *value;
    int location;
} cypher_map_projection_element;

typedef struct cypher_list
{
    ExtensibleNode extensible;
    List *elems;
    int location;
} cypher_list;

enum cypher_string_match_op
{
    CSMO_STARTS_WITH,
    CSMO_ENDS_WITH,
    CSMO_CONTAINS
};

typedef struct cypher_string_match
{
    ExtensibleNode extensible;
    enum cypher_string_match_op operation;
    Node *lhs;
    Node *rhs;
    int location;
} cypher_string_match;

typedef struct cypher_create_target_nodes
{
    ExtensibleNode extensible;
    List *paths;
    uint32 flags;
    uint32 graph_oid;
} cypher_create_target_nodes;

typedef struct cypher_create_path
{
    ExtensibleNode extensible;
    List *target_nodes;
    AttrNumber path_attr_num;
    char *var_name;
} cypher_create_path;

/*
 * comparison expressions
 */

typedef struct cypher_comparison_aexpr
{
    ExtensibleNode extensible;
    A_Expr_Kind kind; /* see above */
    List *name; /* possibly-qualified name of operator */
    Node *lexpr; /* left argument, or NULL if none */
    Node *rexpr; /* right argument, or NULL if none */
    int location; /* token location, or -1 if unknown */
} cypher_comparison_aexpr;

typedef struct cypher_comparison_boolexpr
{
    ExtensibleNode extensible;
    BoolExprType boolop;
    List       *args;           /* arguments to this expression */
    int         location;       /* token location, or -1 if unknown */
} cypher_comparison_boolexpr;


/*
 * procedure call
 */

typedef struct cypher_call
{
    ExtensibleNode extensible;
    FuncCall *funccall; /*from the parser */
    FuncExpr *funcexpr; /*transformed */

    Node *where;
    List *yield_items; /* optional yield subclause */
} cypher_call;

#define CYPHER_CLAUSE_FLAG_NONE 0x0000
#define CYPHER_CLAUSE_FLAG_TERMINAL 0x0001
#define CYPHER_CLAUSE_FLAG_PREVIOUS_CLAUSE 0x0002

#define CYPHER_CLAUSE_IS_TERMINAL(flags) \
    (flags & CYPHER_CLAUSE_FLAG_TERMINAL)

#define CYPHER_CLAUSE_HAS_PREVIOUS_CLAUSE(flags) \
    (flags & CYPHER_CLAUSE_FLAG_PREVIOUS_CLAUSE)

/*
 * Structure that contains all information to create
 * a new entity in the create clause, or where to access
 * this information if it doesn't need to be created.
 *
 * NOTE: This structure may be used for the MERGE clause as
 * well
 */
typedef struct cypher_target_node
{
    ExtensibleNode extensible;
    /* 'v' for vertex or 'e' for edge */
    char type;
    /* flags defined below, prefaced with CYPHER_TARGET_NODE_FLAG_* */
    uint32 flags;
    /* if an edge, denotes direction */
    cypher_rel_dir dir;
    /*
     * Used to create the id for the vertex/edge,
     * if the CYPHER_TARGET_NODE_FLAG_INSERT flag
     * is set. Doing it this way will protect us when
     * rescan gets implemented. By calling the function
     * that creates the id ourselves, we won't have an
     * issue where the id could be created then not used.
     * Since there is a limited number of ids available, we
     * don't want to waste them.
     */
    Expr *id_expr;
    ExprState *id_expr_state;

    Expr *prop_expr;
    ExprState *prop_expr_state;
    /*
     * Attribute Number that this entity's properties
     * are stored in the CustomScanState's child TupleTableSlot
     */
    AttrNumber prop_attr_num;
    /* RelInfo for the table this entity will be stored in */
    ResultRelInfo *resultRelInfo;
    /* elemTupleSlot used to insert the entity into its table */
    TupleTableSlot *elemTupleSlot;
    /* relid that the label stores its entity */
    Oid relid;
    /* label this entity belongs to. */
    char *label_name;
    /* variable name for this entity */
    char *variable_name;
    /*
     * Attribute number this entity needs to be stored in
     * for parent execution nodes to reference it. If the
     * entity is a variable (CYPHER_TARGET_NODE_IS_VAR).
     */
    AttrNumber tuple_position;
} cypher_target_node;

#define CYPHER_TARGET_NODE_FLAG_NONE 0x0000
/* node must insert data */
#define CYPHER_TARGET_NODE_FLAG_INSERT 0x0001
/*
 * Flag that denotes if this target node is referencing
 * a variable that was already created AND created in the
 * same clause.
 */
#define EXISTING_VARIABLE_DECLARED_SAME_CLAUSE 0x0002

/* node is the first instance of a declared variable */
#define CYPHER_TARGET_NODE_IS_VAR 0x0004
/* node is an element in a path variable */
#define CYPHER_TARGET_NODE_IN_PATH_VAR 0x0008

#define CYPHER_TARGET_NODE_MERGE_EXISTS 0x0010

#define CYPHER_TARGET_NODE_OUTPUT(flags) \
    (flags & (CYPHER_TARGET_NODE_IS_VAR | CYPHER_TARGET_NODE_IN_PATH_VAR))

#define CYPHER_TARGET_NODE_IN_PATH(flags) \
    (flags & CYPHER_TARGET_NODE_IN_PATH_VAR)

#define CYPHER_TARGET_NODE_IS_VARIABLE(flags) \
    (flags & CYPHER_TARGET_NODE_IS_VAR)

/*
 * When a vertex is created and is reference in the same clause
 * later. We don't need to check to see if the vertex still exists.
 */
#define SAFE_TO_SKIP_EXISTENCE_CHECK(flags) \
    (flags & EXISTING_VARIABLE_DECLARED_SAME_CLAUSE)

#define CYPHER_TARGET_NODE_INSERT_ENTITY(flags) \
    (flags & CYPHER_TARGET_NODE_FLAG_INSERT)

#define UPDATE_CLAUSE_SET "SET"
#define UPDATE_CLAUSE_REMOVE "REMOVE"

/* Data Structures that contain information about a vertices and edges the need to be updated */
typedef struct cypher_update_information
{
    ExtensibleNode extensible;
    List *set_items;
    uint32 flags;
    AttrNumber tuple_position;
    char *graph_name;
    char *clause_name;
} cypher_update_information;

typedef struct cypher_update_item
{
    ExtensibleNode extensible;
    AttrNumber prop_position;
    AttrNumber entity_position;
    char *var_name;
    char *prop_name;
    List *qualified_name;
    bool remove_item;
    bool is_add;
} cypher_update_item;

typedef struct cypher_delete_information
{
    ExtensibleNode extensible;
    List *delete_items;
    uint32 flags;
    char *graph_name;
    uint32 graph_oid;
    bool detach;
} cypher_delete_information;

typedef struct cypher_delete_item
{
    ExtensibleNode extensible;
    Integer *entity_position;
    char *var_name;
} cypher_delete_item;

typedef struct cypher_merge_information
{
    ExtensibleNode extensible;
    uint32 flags;
    uint32 graph_oid;
    AttrNumber merge_function_attr;
    cypher_create_path *path;
} cypher_merge_information;

/* grammar node for typecasts */
typedef struct cypher_typecast
{
    ExtensibleNode extensible;
    Node *expr;
    char *typecast;
    int location;
} cypher_typecast;

#endif
