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

Yugabyte Cloud Query Language (YCQL) has its roots in the [Cassandra Query Language (CQL)](http://cassandra.apache.org/doc/latest/cql/index.html). This page highlights the important differences in feature support between YCQL and Cassandra 3.4.2.

{{<note>}}
This page does **not** list all the features supported in YCQL. For all the features and commands supported, see [YCQL Reference](../../ycql). For advantages of YCQL over Cassandra, see [Comparison with Apache Cassandra](../../../faq/comparisons/cassandra).
{{</note>}}

## DDL statements

|                    |                Table                |                                          |
| :----------------: | ----------------------------------- | ---------------------------------------: |
| {{<icon/partial>}} | [ALTER TABLE](../ddl_alter_table)   | Cannot rename columns used in an index<br>Cannot Add/Drop multiple columns|
|   {{<icon/no>}}    | ALTER TABLE ... _IF [NOT] EXISTS_   |                                          |
{.sno-1}

|                    |                 Keyspace                  |       |
| :----------------: | ----------------------------------------- | ----: |
|   {{<icon/yes>}}   | [CREATE KEYSPACE](../ddl_create_keyspace) |       |
|   {{<icon/no>}}    | CREATE KEYSPACE ... _IF NOT EXISTS_       |       |
| {{<icon/partial>}} | [ALTER KEYSPACE](../ddl_alter_keyspace)   | No Op |
|   {{<icon/no>}}    | ALTER KEYSPACE _IF EXISTS_                |       |
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
{.sno-1}

## DML statements

|                    |                   Select                    |                                |
| :----------------: | ------------------------------------------- | -----------------------------: |
|   {{<icon/yes>}}   | [... [NOT] IN](../dml_select/)              |                                |
|   {{<icon/yes>}}   | [... IF &lt;expression&gt;](../dml_select/) |                                |
| {{<icon/partial>}} | ... WHERE _CONTAINS_                        | {{<issue 3620 "In Progress">}} |
| {{<icon/partial>}} | ... WHERE _CONTAINS KEY_                    | {{<issue 3620 "In Progress">}} |
|   {{<icon/no>}}    | ... _JSON_                                  |                                |
|   {{<icon/no>}}    | ... _GROUP BY_                              |                                |
|   {{<icon/no>}}    | ... _PER PARTITION LIMIT_                   |                                |
{.sno-1}

|                    |                   Update                    |                         |
| :----------------: | ------------------------------------------- | ----------------------: |
| {{<icon/partial>}} | [UPDATE](../dml_update/)                    | Only single row updates |
| {{<icon/partial>}} | [... [NOT] IN](../dml_update/)              | Only single row updates |
|   {{<icon/yes>}}   | [... IF &lt;expression&gt;](../dml_update/) |                         |
|   {{<icon/yes>}}   | [... USING TTL](../dml_update/)             |                         |
|   {{<icon/no>}}    | ... _CONTAINS [KEY]_                        |                         |
{.sno-1}

|                    |                   Delete                    |                         |
| :----------------: | ------------------------------------------- | ----------------------: |
| {{<icon/partial>}} | [DELETE](../dml_delete/)                    | Only single row deletes |
| {{<icon/partial>}} | [... [NOT] IN](../dml_delete/)              | Only single row deletes |
|   {{<icon/yes>}}   | [... IF &lt;expression&gt;](../dml_delete/) |                         |
|   {{<icon/yes>}}   | [... USING TTL](../dml_delete/)             |                         |
|   {{<icon/no>}}    | ... _CONTAINS [KEY]_                        |                         |
{.sno-1}

|                |         Insert          |
| :------------: | ----------------------- |
| {{<icon/yes>}} | [INSERT](../dml_insert) |
| {{<icon/no>}}  | INSERT _JSON_           |
{.sno-1}

|                    |              Feature              |                                          |
| :----------------: | --------------------------------- | ---------------------------------------: |
|   {{<icon/no>}}    | Materialized Views                |                                          |
| {{<icon/partial>}} | BATCH                             | Only programattically via BatchStatement |
{.sno-1}

## Unsupported datatypes

|               | Type  |
| :-----------: | ----- |
| {{<icon/no>}} | Tuple |
{.sno-1}

## Unsupported commands

|               |                       Feature                       |                                             |
| :-----------: | --------------------------------------------------- | ------------------------------------------: |
| {{<icon/no>}} | [CREATE &vert; DROP] FUNCTION                       |  No support for User Defined Function (UDF) |
| {{<icon/no>}} | [CREATE &vert; DROP] AGGREGATE                      | No support for User Defined Aggregates(UDA) |
| {{<icon/no>}} | [CREATE &vert; DROP] TRIGGER                        |                     No support for Triggers |
| {{<icon/no>}} | [CREATE &vert; DROP &vert; ALTER  &vert; LIST] USER |                    Legacy Cassandra feature |
| {{<icon/no>}} | LIST ROLES                                          |                                             |
| {{<icon/no>}} | LIST PERMISSION                                     |                                             |
{.sno-1}

## Learn more

- [Comparison with Apache Cassandra](../../../faq/comparisons/cassandra)
- [YCQL command reference](../)
