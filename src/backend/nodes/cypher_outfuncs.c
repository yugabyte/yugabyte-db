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

#include "postgres.h"

#include "lib/stringinfo.h"
#include "nodes/extensible.h"
#include "nodes/nodes.h"

#include "nodes/cypher_nodes.h"
#include "nodes/cypher_outfuncs.h"

static void outChar(StringInfo str, char c);

#define DEFINE_AG_NODE(type) \
    type *_node = (type *)node

#define WRITE_NODE_FIELD(field_name) \
    do \
    { \
        appendStringInfoString(str, " :" CppAsString(field_name) " "); \
        outNode(str, _node->field_name); \
    } while (0)

#define WRITE_STRING_FIELD(field_name) \
    do \
    { \
        appendStringInfoString(str, " :" CppAsString(field_name) " "); \
        outToken(str, _node->field_name); \
    } while (0)

// Write a char field (ie, one ascii character)
#define WRITE_CHAR_FIELD(fldname) \
    do { \
        (appendStringInfo(str, " :" CppAsString(fldname) " "), \
         outChar(str, _node->fldname)); \
    } while (0)

#define WRITE_BOOL_FIELD(field_name) \
    do \
    { \
        appendStringInfo(str, " :" CppAsString(field_name) " %s", \
                         _node->field_name ? "true" : "false"); \
    } while (0)

// write an enumerated-type field as an integer code
#define WRITE_ENUM_FIELD(field_name, enum_type) \
    do \
    { \
        appendStringInfo(str, " :" CppAsString(field_name) " %d", \
                         (int)_node->field_name); \
    } while (0)

#define WRITE_LOCATION_FIELD(field_name) \
    do \
    { \
        appendStringInfo(str, " :" CppAsString(field_name) " %d", \
                         _node->field_name); \
    } while (0)

#define WRITE_INT64_FIELD(field_name) \
    do \
    { \
        appendStringInfo(str, " :" CppAsString(field_name) " %ld", \
                         _node->field_name); \
    } while (0)

// Write an integer field (anything written as ":fldname %d")
#define WRITE_INT32_FIELD(field_name) \
    do { \
        appendStringInfo(str, " :" CppAsString(field_name) " %d", \
                         _node->field_name); \
    } while (0)

// Write an OID field (don't hard-wire assumption that OID is same as uint)
#define WRITE_OID_FIELD(fldname) \
    do { \
        appendStringInfo(str, " :" CppAsString(fldname) " %u", \
                          _node->fldname); \
    } while(0);



// serialization function for the cypher_return ExtensibleNode.
void out_cypher_return(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_return);

    WRITE_BOOL_FIELD(distinct);
    WRITE_NODE_FIELD(items);
    WRITE_NODE_FIELD(order_by);
    WRITE_NODE_FIELD(skip);
    WRITE_NODE_FIELD(limit);

    WRITE_BOOL_FIELD(all_or_distinct);
    WRITE_ENUM_FIELD(op, SetOperation);
    WRITE_NODE_FIELD(larg);
    WRITE_NODE_FIELD(rarg);
}

// serialization function for the cypher_with ExtensibleNode.
void out_cypher_with(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_with);

    WRITE_BOOL_FIELD(distinct);
    WRITE_NODE_FIELD(items);
    WRITE_NODE_FIELD(order_by);
    WRITE_NODE_FIELD(skip);
    WRITE_NODE_FIELD(limit);
    WRITE_NODE_FIELD(where);
}

// serialization function for the cypher_match ExtensibleNode.
void out_cypher_match(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_match);

    WRITE_NODE_FIELD(pattern);
    WRITE_NODE_FIELD(where);
    WRITE_BOOL_FIELD(optional);
}

// serialization function for the cypher_create ExtensibleNode.
void out_cypher_create(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_create);

    WRITE_NODE_FIELD(pattern);
}

// serialization function for the cypher_set ExtensibleNode.
void out_cypher_set(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_set);

    WRITE_NODE_FIELD(items);
    WRITE_BOOL_FIELD(is_remove);
}

// serialization function for the cypher_set_item ExtensibleNode.
void out_cypher_set_item(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_set_item);

    WRITE_NODE_FIELD(prop);
    WRITE_NODE_FIELD(expr);
    WRITE_BOOL_FIELD(is_add);
}

// serialization function for the cypher_delete ExtensibleNode.
void out_cypher_delete(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_delete);

    WRITE_BOOL_FIELD(detach);
    WRITE_NODE_FIELD(exprs);
}

void out_cypher_unwind(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_unwind);

    WRITE_NODE_FIELD(target);
}

// serialization function for the cypher_delete ExtensibleNode.
void out_cypher_merge(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_merge);

    WRITE_NODE_FIELD(path);
}

// serialization function for the cypher_path ExtensibleNode.
void out_cypher_path(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_path);

    WRITE_NODE_FIELD(path);
    WRITE_STRING_FIELD(var_name);
    WRITE_STRING_FIELD(parsed_var_name);
    WRITE_LOCATION_FIELD(location);
}

// serialization function for the cypher_node ExtensibleNode.
void out_cypher_node(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_node);

    WRITE_STRING_FIELD(name);
    WRITE_STRING_FIELD(parsed_name);
    WRITE_STRING_FIELD(label);
    WRITE_STRING_FIELD(parsed_label);
    WRITE_NODE_FIELD(props);
    WRITE_LOCATION_FIELD(location);
}

// serialization function for the cypher_relationship ExtensibleNode.
void out_cypher_relationship(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_relationship);

    WRITE_STRING_FIELD(name);
    WRITE_STRING_FIELD(parsed_name);
    WRITE_STRING_FIELD(label);
    WRITE_STRING_FIELD(parsed_label);
    WRITE_NODE_FIELD(props);
    WRITE_NODE_FIELD(varlen);
    WRITE_ENUM_FIELD(dir, cypher_rel_dir);
    WRITE_LOCATION_FIELD(location);
}

// serialization function for the cypher_bool_const ExtensibleNode.
void out_cypher_bool_const(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_bool_const);

    WRITE_BOOL_FIELD(boolean);
    WRITE_LOCATION_FIELD(location);
}

// serialization function for the cypher_param ExtensibleNode.
void out_cypher_param(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_param);

    WRITE_STRING_FIELD(name);
    WRITE_LOCATION_FIELD(location);
}

// serialization function for the cypher_map ExtensibleNode.
void out_cypher_map(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_map);

    WRITE_NODE_FIELD(keyvals);
    WRITE_LOCATION_FIELD(location);
}

// serialization function for the cypher_list ExtensibleNode.
void out_cypher_list(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_list);

    WRITE_NODE_FIELD(elems);
    WRITE_LOCATION_FIELD(location);
}

// serialization function for the cypher_comparison_aexpr ExtensibleNode.
void out_cypher_comparison_aexpr(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_comparison_aexpr);

    WRITE_ENUM_FIELD(kind, A_Expr_Kind);
    WRITE_NODE_FIELD(name);
    WRITE_NODE_FIELD(lexpr);
    WRITE_NODE_FIELD(rexpr);
    WRITE_LOCATION_FIELD(location);
}

// serialization function for the cypher_comparison_boolexpr ExtensibleNode.
void out_cypher_comparison_boolexpr(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_comparison_boolexpr);

    WRITE_ENUM_FIELD(boolop, BoolExprType);
    WRITE_NODE_FIELD(args);
    WRITE_LOCATION_FIELD(location);
}

// serialization function for the cypher_string_match ExtensibleNode.
void out_cypher_string_match(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_string_match);

    WRITE_ENUM_FIELD(operation, cypher_string_match_op);
    WRITE_NODE_FIELD(lhs);
    WRITE_NODE_FIELD(rhs);
    WRITE_LOCATION_FIELD(location);
}

// serialization function for the cypher_typecast ExtensibleNode.
void out_cypher_typecast(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_typecast);

    WRITE_NODE_FIELD(expr);
    WRITE_STRING_FIELD(typecast);
    WRITE_LOCATION_FIELD(location);
}

// serialization function for the cypher_integer_const ExtensibleNode.
void out_cypher_integer_const(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_integer_const);

    WRITE_INT64_FIELD(integer);
    WRITE_LOCATION_FIELD(location);
}

// serialization function for the cypher_sub_pattern ExtensibleNode.
void out_cypher_sub_pattern(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_sub_pattern);

    WRITE_ENUM_FIELD(kind, csp_kind);
    WRITE_NODE_FIELD(pattern);
}

// serialization function for the cypher_call ExtensibleNode.
void out_cypher_call(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_call);

    WRITE_NODE_FIELD(funccall);
    WRITE_NODE_FIELD(funcexpr);
    WRITE_NODE_FIELD(where);
    WRITE_NODE_FIELD(yield_items);
}

// serialization function for the cypher_create_target_nodes ExtensibleNode.
void out_cypher_create_target_nodes(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_create_target_nodes);

    WRITE_NODE_FIELD(paths);
    WRITE_INT32_FIELD(flags);
    WRITE_INT32_FIELD(graph_oid);
}

// serialization function for the cypher_create_path ExtensibleNode.
void out_cypher_create_path(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_create_path);

    WRITE_NODE_FIELD(target_nodes);
    WRITE_INT32_FIELD(path_attr_num);
    WRITE_STRING_FIELD(var_name);
}

// serialization function for the cypher_target_node ExtensibleNode.
void out_cypher_target_node(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_target_node);

    WRITE_CHAR_FIELD(type);
    WRITE_INT32_FIELD(flags);
    WRITE_ENUM_FIELD(dir, cypher_rel_dir);
    WRITE_NODE_FIELD(id_expr);
    WRITE_NODE_FIELD(id_expr_state);
    WRITE_NODE_FIELD(prop_expr);
    WRITE_NODE_FIELD(prop_expr_state);
    WRITE_INT32_FIELD(prop_attr_num);
    WRITE_NODE_FIELD(resultRelInfo);
    WRITE_NODE_FIELD(elemTupleSlot);
    WRITE_OID_FIELD(relid);
    WRITE_STRING_FIELD(label_name);
    WRITE_STRING_FIELD(variable_name);
    WRITE_INT32_FIELD(tuple_position);
}

// serialization function for the cypher_update_information ExtensibleNode.
void out_cypher_update_information(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_update_information);

    WRITE_NODE_FIELD(set_items);
    WRITE_INT32_FIELD(flags);
    WRITE_INT32_FIELD(tuple_position);
    WRITE_STRING_FIELD(graph_name);
    WRITE_STRING_FIELD(clause_name);
}

// serialization function for the cypher_update_item ExtensibleNode.
void out_cypher_update_item(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_update_item);

    WRITE_INT32_FIELD(prop_position);
    WRITE_INT32_FIELD(entity_position);
    WRITE_STRING_FIELD(var_name);
    WRITE_STRING_FIELD(prop_name);
    WRITE_NODE_FIELD(qualified_name);
    WRITE_BOOL_FIELD(remove_item);
    WRITE_BOOL_FIELD(is_add);
}

// serialization function for the cypher_delete_information ExtensibleNode.
void out_cypher_delete_information(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_delete_information);

    WRITE_NODE_FIELD(delete_items);
    WRITE_INT32_FIELD(flags);
    WRITE_STRING_FIELD(graph_name);
    WRITE_INT32_FIELD(graph_oid);
    WRITE_BOOL_FIELD(detach);
}

// serialization function for the cypher_delete_item ExtensibleNode.
void out_cypher_delete_item(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_delete_item);

    WRITE_NODE_FIELD(entity_position);
    WRITE_STRING_FIELD(var_name);
}

// serialization function for the cypher_merge_information ExtensibleNode.
void out_cypher_merge_information(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_merge_information);

    WRITE_INT32_FIELD(flags);
    WRITE_INT32_FIELD(graph_oid);
    WRITE_INT32_FIELD(merge_function_attr);
    WRITE_NODE_FIELD(path);
}

/*
 * Copied from Postgres
 *
 * Convert one char.  Goes through outToken() so that special characters are
 * escaped.
 */
static void
outChar(StringInfo str, char c)
{
        char            in[2];

        in[0] = c;
        in[1] = '\0';

        outToken(str, in);
}

