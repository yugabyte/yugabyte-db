---
title: YCQL - Cassandra 3.4 compatibility
headerTitle: YCQL - Cassandra 3.4 compatibility
linkTitle: Cassandra compatibility
description: Feature parity with Cassandra 3.4
summary: Reference for the YCQL API
image: /images/section_icons/api/ycql.png
headcontent:
menu:
  preview:
    parent: api-cassandra
    weight: 1190
type: docs
---

## Overview

Yugabyte Cloud Query Language (YCQL) has its roots in the [Cassandra Query Language (CQL)](http://cassandra.apache.org/doc/latest/cql/index.html). This page highlights the important feature support differences between YCQL and Cassandra 3.4.2.

{{<warning>}}
This page does **not** list all the features supported in YCQL.
{{</warning>}}

## DDL statements
|                    |                Table                |                                          |
| :----------------: | ----------------------------------- | ---------------------------------------: |
|   {{<icon/yes>}}   | [CREATE TABLE](../ddl_create_table) |                                          |
| {{<icon/partial>}} | [ALTER TABLE](../ddl_alter_table)   | Cannot rename columns used in an index<br> Cannot Add/Drop multiple columns|
|   {{<icon/no>}}    | ALTER TABLE ... _IF [NOT] EXISTS_   |                                          |
|   {{<icon/yes>}}   | [DROP TABLE](../ddl_drop_table)     |                                          |
{.sno-1}

|                    |                     Keyspace                      |       |
| :----------------: | ------------------------------------------------- | ----: |
|   {{<icon/yes>}}   | [CREATE KEYSPACE](../ddl_create_keyspace)         |       |
|   {{<icon/no>}}    | CREATE KEYSPACE ... _IF NOT EXISTS_                 |       |
| {{<icon/partial>}} | [ALTER KEYSPACE](../ddl_alter_keyspace)           | No Op |
|   {{<icon/no>}}    | ALTER KEYSPACE _IF EXISTS_                      |       |
|   {{<icon/yes>}}   | [DROP KEYSPACE](../ddl_drop_keyspace) [IF EXISTS] |       |
|   {{<icon/yes>}}   | [USE](../ddl_use)                                 |       |
{.sno-1}

|                |                Index                 |    |
| :------------: | ------------------------------------ | -: |
| {{<icon/partial>}} | [CREATE&nbsp;INDEX](../ddl_create_index/) | No support for `map/list/set/tuple/full jsonb/udt` and <br> `keys/values/entries` of a collection |
| {{<icon/yes>}} | [DROP INDEX](../ddl_drop_index)      |    |
{.sno-1}

|                |               Type                |
| :------------: | --------------------------------- |
| {{<icon/yes>}} | [CREATE TYPE](../ddl_create_type) |
| {{<icon/no>}}  | ALTER TYPE                        |
| {{<icon/yes>}} | [DROP TYPE](../ddl_drop_type)     |
{.sno-1}

## DML statements

|                    |                   Select                    |                                |
| :----------------: | ------------------------------------------- | -----------------------------: |
|   {{<icon/yes>}}   | [... [NOT] IN](../dml_select/)              |                                |
|   {{<icon/yes>}}   | [... IF &lt;expression&gt;](../dml_select/) |                                |
| {{<icon/partial>}} | ... WHERE _CONTAINS_                      | {{<issue 3620 "In Progress">}} |
| {{<icon/partial>}} | ... WHERE _CONTAINS KEY_                  | {{<issue 3620 "In Progress">}} |
|   {{<icon/no>}}    | ... _JSON_                                |                                |
|   {{<icon/no>}}    | ... _GROUP BY_                            |                                |
|   {{<icon/no>}}    | ... _PER PARTITION LIMIT_                 |                                |
{.sno-1}

|                    |                   Update                    |                         |
| :----------------: | ------------------------------------------- | ----------------------: |
| {{<icon/partial>}} | [UPDATE](../dml_update/)                    | Only single row updates |
| {{<icon/partial>}} | [... [NOT] IN](../dml_update/)              | Only single row updates |
|   {{<icon/yes>}}   | [... IF &lt;expression&gt;](../dml_update/) |                         |
|   {{<icon/yes>}}   | [... USING TTL](../dml_update/)              |                         |
|   {{<icon/no>}}    | ... _CONTAINS [KEY]_                       |                         |
{.sno-1}

|                    |                   Delete                    |                         |
| :----------------: | ------------------------------------------- | ----------------------: |
| {{<icon/partial>}} | [DELETE](../dml_delete/)                    | Only single row deletes |
| {{<icon/partial>}} | [... [NOT] IN](../dml_delete/)              | Only single row deletes |
|   {{<icon/yes>}}   | [... IF &lt;expression&gt;](../dml_delete/) |                         |
|   {{<icon/yes>}}   | [... USING TTL](../dml_delete/)              |                         |
|   {{<icon/no>}}    | ... _CONTAINS [KEY]_                      |                         |
{.sno-1}

|                |         Insert          |
| :------------: | ----------------------- |
| {{<icon/yes>}} | [INSERT](../dml_insert) |
| {{<icon/no>}}  | INSERT _JSON_         |
{.sno-1}

|                    |              Feature              |                                          |
| :----------------: | --------------------------------- | ---------------------------------------: |
|   {{<icon/yes>}}   | [TRANSACTION](../dml_transaction) |                                          |
|   {{<icon/yes>}}   | [TRUNCATE](../dml_truncate)       |                                          |
|   {{<icon/no>}}    | Materialized Views                |                                          |
| {{<icon/partial>}} | BATCH                             | Only programattically via BatchStatement |
{.sno-1}

## DDL security statements

|                |               Role                |
| :------------: | --------------------------------- |
| {{<icon/yes>}} | [CREATE ROLE](../ddl_create_role) |
| {{<icon/yes>}} | [ALTER ROLE](../ddl_alter_role)   |
| {{<icon/yes>}} | [GRANT ROLE](../ddl_grant_role)   |
| {{<icon/yes>}} | [REVOKE ROLE](../ddl_revoke_role) |
| {{<icon/yes>}} | [DROP ROLE](../ddl_drop_role)     |
| {{<icon/no>}}  | LIST ROLES                        |
{.sno-1}


|                |                  Permission                   |
| :------------: | --------------------------------------------- |
| {{<icon/yes>}} | [GRANT PERMISSION](../ddl_grant_permission)   |
| {{<icon/yes>}} | [REVOKE PERMISSION](../ddl_revoke_permission) |
| {{<icon/no>}}  | LIST PERMISSION                               |
{.sno-1}

## Datatypes

|                |                                    Type                                    |
| :------------: | -------------------------------------------------------------------------- |
| {{<icon/yes>}} | [JSONB](../type_jsonb)                                                     |
| {{<icon/yes>}} | [JSON Operators: `->` , `->>` ](../type_jsonb)                             |
| {{<icon/yes>}} | [Primitive Types: Boolean, Int, Float, Decimal ...](../../ycql#data-types) |
| {{<icon/yes>}} | [Collections: Set, List, Map](../type_collection)                          |
| {{<icon/yes>}} | [Frozen](../type_frozen)                                                   |
| {{<icon/no>}}  | Tuple                                                                      |
{.sno-1}

## Functions

|                |                                          Type                                           |
| :------------: | --------------------------------------------------------------------------------------- |
| {{<icon/yes>}} | [CAST](../expr_fcall#cast-function)                                                     |
| {{<icon/yes>}} | [Aggregate: `count/min/ma/avg/sum`](../expr_fcall#aggregate_functions)                  |
| {{<icon/yes>}} | [Primitive Types: `Boolean, Int, Float, Decimal ...`](../../ycql#data-types)            |
| {{<icon/yes>}} | [Collection: Add/Remove on `Set/List/Map`.](../type_collection/#collection-expressions) |
{.sno-1}

## Unsupported Features

|               |                       Feature                       |                                             |
| :-----------: | --------------------------------------------------- | ------------------------------------------: |
| {{<icon/no>}} | [CREATE &vert; DROP] FUNCTION                       | No support for User Defined Function (UDF)  |
| {{<icon/no>}} | [CREATE &vert; DROP] AGGREGATE                      | No support for User Defined Aggregates(UDA) |
| {{<icon/no>}} | [CREATE &vert; DROP] TRIGGER                        | No support for Triggers                     |
| {{<icon/no>}} | [CREATE &vert; DROP &vert; ALTER  &vert; LIST] USER | Legacy Cassandra feature                    |
{.sno-1}
