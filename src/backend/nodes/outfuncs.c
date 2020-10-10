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

#include "postgres.h"

#include "lib/stringinfo.h"
#include "nodes/extensible.h"
#include "nodes/nodes.h"

#include "nodes/cypher_nodes.h"

#define DEFINE_AG_NODE(type) \
    type *_node = (type *)node

#define write_node_field(field_name) \
    do \
    { \
        appendStringInfoString(str, " :" CppAsString(field_name) " "); \
        outNode(str, _node->field_name); \
    } while (0)

#define write_string_field(field_name) \
    do \
    { \
        appendStringInfoString(str, " :" CppAsString(field_name) " "); \
        outToken(str, _node->field_name); \
    } while (0)

#define write_bool_field(field_name) \
    do \
    { \
        appendStringInfo(str, " :" CppAsString(field_name) " %s", \
                         _node->field_name ? "true" : "false"); \
    } while (0)

// write an enumerated-type field as an integer code
#define write_enum_field(field_name, enum_type) \
    do \
    { \
        appendStringInfo(str, " :" CppAsString(field_name) " %d", \
                         (int)_node->field_name); \
    } while (0)

#define write_location_field(field_name) \
    do \
    { \
        appendStringInfo(str, " :" CppAsString(field_name) " %d", \
                         _node->field_name); \
    } while (0)

#define write_int64_field(field_name) \
    do \
    { \
        appendStringInfo(str, " :" CppAsString(field_name) " %ld", \
                         _node->field_name); \
    } while (0)

/*
 * clauses
 */

void out_cypher_return(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_return);

    write_bool_field(distinct);
    write_node_field(items);
    write_node_field(order_by);
    write_node_field(skip);
    write_node_field(limit);
}

void out_cypher_with(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_with);

    write_bool_field(distinct);
    write_node_field(items);
    write_node_field(order_by);
    write_node_field(skip);
    write_node_field(limit);
    write_node_field(where);
}

void out_cypher_match(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_match);

    write_node_field(pattern);
    write_node_field(where);
}

void out_cypher_create(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_create);

    write_node_field(pattern);
}

void out_cypher_set(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_set);

    write_node_field(items);
    write_bool_field(is_remove);
}

void out_cypher_set_item(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_set_item);

    write_node_field(prop);
    write_node_field(expr);
    write_bool_field(is_add);
}

void out_cypher_delete(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_delete);

    write_bool_field(detach);
    write_node_field(exprs);
}

/*
 * pattern
 */

void out_cypher_path(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_path);

    write_node_field(path);
    write_location_field(location);
}

void out_cypher_node(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_node);

    write_string_field(name);
    write_string_field(label);
    write_node_field(props);
    write_location_field(location);
}

void out_cypher_relationship(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_relationship);

    write_string_field(name);
    write_string_field(label);
    write_node_field(props);
    write_enum_field(dir, cypher_rel_dir);
}

/*
 * expression
 */

void out_cypher_bool_const(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_bool_const);

    write_bool_field(boolean);
    write_location_field(location);
}

void out_cypher_param(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_param);

    write_string_field(name);
    write_location_field(location);
}

void out_cypher_map(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_map);

    write_node_field(keyvals);
    write_location_field(location);
}

void out_cypher_list(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_list);

    write_node_field(elems);
    write_location_field(location);
}

/*
 * string match
 */

void out_cypher_string_match(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_string_match);

    write_enum_field(operation, cypher_string_match_op);
    write_node_field(lhs);
    write_node_field(rhs);
    write_location_field(location);
}

/* typecast */
void out_cypher_typecast(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_typecast);

    write_node_field(expr);
    write_string_field(typecast);
    write_location_field(location);
}

/* function */
void out_cypher_function(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_function);

    write_node_field(exprs);
    write_node_field(funcname);
    write_location_field(location);
}

/* integer constant */
void out_cypher_integer_const(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_integer_const);

    write_int64_field(integer);
    write_location_field(location);
}

/* sub pattern */
void out_cypher_sub_pattern(StringInfo str, const ExtensibleNode *node)
{
    DEFINE_AG_NODE(cypher_sub_pattern);

    write_enum_field(kind, csp_kind);
    write_node_field(pattern);
}
