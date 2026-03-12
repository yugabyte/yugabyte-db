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

#include "nodes/readfuncs.h"

#include "nodes/cypher_readfuncs.h"
#include "nodes/cypher_nodes.h"

/*
 * Copied From Postgres
 *
 * Macros for declaring appropriate local variables.
 */
/* A few guys need only local_node */
#define READ_LOCALS_NO_FIELDS(nodeTypeName) \
    nodeTypeName *local_node = (nodeTypeName *) node

/* And a few guys need only the pg_strtok support fields */
#define READ_TEMP_LOCALS() \
    const char *token; \
    int length

/* ... but most need both */
#define READ_LOCALS(nodeTypeName) \
    READ_LOCALS_NO_FIELDS(nodeTypeName); \
    READ_TEMP_LOCALS()

/*
 * The READ_*_FIELD defines first skips the :fldname token (key) part of the string
 * and then converts the next token (value) to the correct data type.
 *
 * pg_strtok will split the passed string by whitespace, skipping whitespace in
 * strings. We do not setup pg_strtok. That is for the caller to do. By default
 * that is the responsibility of Postgres' nodeRead function. We assume that was setup
 * correctly.
 */

/* Read an integer field (anything written as ":fldname %d") */
#define READ_INT_FIELD(fldname) \
        token = pg_strtok(&length); \
        token = pg_strtok(&length); \
        local_node->fldname = strtol(token, 0, 10)

/* Read an unsigned integer field (anything written as ":fldname %u") */
#define READ_UINT_FIELD(fldname) \
        token = pg_strtok(&length); \
        token = pg_strtok(&length); \
        local_node->fldname = atoui(token)
/* Read an unsigned integer field (anything written using UINT64_FORMAT) */
#define READ_UINT64_FIELD(fldname) \
        token = pg_strtok(&length); \
        token = pg_strtok(&length); \
        local_node->fldname = pg_strtouint64(token, NULL, 10)

/* Read a long integer field (anything written as ":fldname %ld") */
#define READ_LONG_FIELD(fldname) \
        token = pg_strtok(&length); \
        token = pg_strtok(&length); \
        local_node->fldname = atol(token)

/* Read an OID field (don't hard-wire assumption that OID is same as uint) */
#define READ_OID_FIELD(fldname) \
        token = pg_strtok(&length); \
        token = pg_strtok(&length); \
        local_node->fldname = atooid(token)

/* Read a char field (ie, one ascii character) */
#define READ_CHAR_FIELD(fldname) \
        token = pg_strtok(&length); \
        token = pg_strtok(&length); \
        /* avoid overhead of calling debackslash() for one char */ \
        local_node->fldname = (length == 0) ? '\0' : (token[0] == '\\' ? token[1] : token[0])

/* Read an enumerated-type field that was written as an integer code */
#define READ_ENUM_FIELD(fldname, enumtype) \
        token = pg_strtok(&length); \
        token = pg_strtok(&length); \
        local_node->fldname = (enumtype) strtol(token, 0, 10)

/* Read a float field */
#define READ_FLOAT_FIELD(fldname) \
        token = pg_strtok(&length); \
        token = pg_strtok(&length); \
        local_node->fldname = atof(token)

/* Read a boolean field */
#define READ_BOOL_FIELD(fldname) \
        token = pg_strtok(&length); \
        token = pg_strtok(&length); \
        local_node->fldname = strtobool(token)

/* Read a character-string field */
#define READ_STRING_FIELD(fldname) \
        token = pg_strtok(&length); \
        token = pg_strtok(&length); \
        local_node->fldname = non_nullable_string(token, length)

/* Read a parse location field (and throw away the value, per notes above) */
#define READ_LOCATION_FIELD(fldname) \
        token = pg_strtok(&length); \
        token = pg_strtok(&length); \
        (void) token; \
        local_node->fldname = -1

/* Read a Node field */
#define READ_NODE_FIELD(fldname) \
        token = pg_strtok(&length);  \
        (void) token; \
        local_node->fldname = nodeRead(NULL, 0)

/* Read a bitmapset field */
#define READ_BITMAPSET_FIELD(fldname) \
        token = pg_strtok(&length); \
        (void) token; \
        local_node->fldname = _readBitmapset()

/* Read an attribute number array */
#define READ_ATTRNUMBER_ARRAY(fldname, len) \
        token = pg_strtok(&length); \
        local_node->fldname = readAttrNumberCols(len);

/* Read an oid array */
#define READ_OID_ARRAY(fldname, len) \
        token = pg_strtok(&length); \
        local_node->fldname = readOidCols(len);

/* Read an int array */
#define READ_INT_ARRAY(fldname, len) \
        token = pg_strtok(&length); \
        local_node->fldname = readIntCols(len);

/* Read a bool array */
#define READ_BOOL_ARRAY(fldname, len) \
        token = pg_strtok(&length); \
        local_node->fldname = readBoolCols(len);

/*
 * NOTE: use atoi() to read values written with %d, or atoui() to read
 * values written with %u in outfuncs.c.  An exception is OID values,
 * for which use atooid().  (As of 7.1, outfuncs.c writes OIDs as %u,
 * but this will probably change in the future.)
 */
#define atoui(x)  ((unsigned int) strtoul((x), NULL, 10))

#define strtobool(x)  ((*(x) == 't') ? true : false)

#define nullable_string(token,length)  \
        ((length) == 0 ? NULL : debackslash(token, length))

#define non_nullable_string(token,length)  \
        ((length) == 0 ? "" : debackslash(token, length))

/*
 * Default read function for cypher nodes. For most nodes, we don't expect
 * the node to ever be read (deserialized). So throw an error.
 */
void read_ag_node(ExtensibleNode *node)
{
    ereport(ERROR, (errmsg("unexpected parseNodeString() for ag_node")));
}

/*
 * Deserialize a string representing the cypher_create_target_nodes
 * data structure.
 */
void read_cypher_create_target_nodes(struct ExtensibleNode *node)
{
    READ_LOCALS(cypher_create_target_nodes);

    READ_NODE_FIELD(paths);
    READ_UINT_FIELD(flags);
    READ_UINT_FIELD(graph_oid);
}

/*
 * Deserialize a string representing the cypher_create_path
 * data structure.
 */
void read_cypher_create_path(struct ExtensibleNode *node)
{
    READ_LOCALS(cypher_create_path);

    READ_NODE_FIELD(target_nodes);
    READ_INT_FIELD(path_attr_num);
    READ_STRING_FIELD(var_name);
}

/*
 * Deserialize a string representing the cypher_target_node
 * data structure.
 */
void read_cypher_target_node(struct ExtensibleNode *node)
{
    READ_LOCALS(cypher_target_node);

    READ_CHAR_FIELD(type);
    READ_UINT_FIELD(flags);
    READ_ENUM_FIELD(dir, cypher_rel_dir);
    READ_NODE_FIELD(id_expr);
    READ_NODE_FIELD(id_expr_state);
    READ_NODE_FIELD(prop_expr);
    READ_NODE_FIELD(prop_expr_state);
    READ_INT_FIELD(prop_attr_num);
    READ_NODE_FIELD(resultRelInfo);
    READ_NODE_FIELD(elemTupleSlot);
    READ_OID_FIELD(relid);
    READ_STRING_FIELD(label_name);
    READ_STRING_FIELD(variable_name);
    READ_INT_FIELD(tuple_position);
}

/*
 * Deserialize a string representing the cypher_update_information
 * data structure.
 */
void read_cypher_update_information(struct ExtensibleNode *node)
{
    READ_LOCALS(cypher_update_information);

    READ_NODE_FIELD(set_items);
    READ_UINT_FIELD(flags);
    READ_INT_FIELD(tuple_position);
    READ_STRING_FIELD(graph_name);
    READ_STRING_FIELD(clause_name);
}

/*
 * Deserialize a string representing the cypher_update_item
 * data structure.
 */
void read_cypher_update_item(struct ExtensibleNode *node)
{
    READ_LOCALS(cypher_update_item);

    READ_INT_FIELD(prop_position);
    READ_INT_FIELD(entity_position);
    READ_STRING_FIELD(var_name);
    READ_STRING_FIELD(prop_name);
    READ_NODE_FIELD(qualified_name);
    READ_BOOL_FIELD(remove_item);
    READ_BOOL_FIELD(is_add);
}

/*
 * Deserialize a string representing the cypher_delete_information
 * data structure.
 */
void read_cypher_delete_information(struct ExtensibleNode *node)
{
    READ_LOCALS(cypher_delete_information);

    READ_NODE_FIELD(delete_items);
    READ_UINT_FIELD(flags);
    READ_STRING_FIELD(graph_name);
    READ_UINT_FIELD(graph_oid);
    READ_BOOL_FIELD(detach);
}

/*
 * Deserialize a string representing the cypher_delete_item
 * data structure.
 */
void read_cypher_delete_item(struct ExtensibleNode *node)
{
    READ_LOCALS(cypher_delete_item);

    READ_NODE_FIELD(entity_position);
    READ_STRING_FIELD(var_name);
}

/*
 * Deserialize a string representing the cypher_merge_information
 * data structure.
 */
void read_cypher_merge_information(struct ExtensibleNode *node)
{
    READ_LOCALS(cypher_merge_information);

    READ_UINT_FIELD(flags);
    READ_UINT_FIELD(graph_oid);
    READ_INT_FIELD(merge_function_attr);
    READ_NODE_FIELD(path);
}
