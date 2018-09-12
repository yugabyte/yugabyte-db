---
title: YCQL
linkTitle: YCQL
description: YugaByte Cloud Query Language (YCQL)
summary: Reference for Cassandra-compatible YCQL API
image: /images/section_icons/api/cql.png
headcontent: 
menu:
  1.0:
    identifier: api-cassandra
    parent: api
    weight: 1300
aliases:
  - api/cassandra/
  - api/cql/
  - api/ycql/
---

## Introduction
YugaByte DB's YCQL API is compatible with the Cassandra Query Language (CQL) and supports the following features.

- Data definition language (DDL) statements.
- Data manipulation language (DML) statements.
- Builtin functions and Expression operators.
- Primitive user-defined datatypes.

## DDL Statements
Data definition language (DDL) statements are instructions for the following database operations.

- Create, alter, and drop database objects
- Create, grant, and revoke users and roles

Statement | Description |
----------|-------------|
[`ALTER TABLE`](ddl_alter_table) | Alter a table |
[`CREATE INDEX`](ddl_create_index) | Create a new index on a table |
[`CREATE KEYSPACE`](ddl_create_keyspace) | Create a new keyspace |
[`CREATE TABLE`](ddl_create_table) | Create a new table |
[`CREATE TYPE`](ddl_create_type) | Create a user-defined datatype |
[`DROP KEYSPACE`](ddl_drop_keyspace) | Delete a keyspace and associated objects |
[`DROP TABLE`](ddl_drop_table) | Remove a table |
[`DROP TYPE`](ddl_drop_type) | Remove a user-defined datatype |
[`USE`](ddl_use) | Use an existing keyspace for subsequent commands |

## DML Statements
Data manipulation language (DML) statements are used to read from and write to the existing database objects. YugaByte DB implicitly commits any updates by DML statements (similar to how Apache Cassandra behaves).

Statement | Description |
----------|-------------|
[`INSERT`](dml_insert) | Insert rows into a table |
[`SELECT`](dml_select) | Select rows from a table |
[`UPDATE`](dml_update) | Update rows in a table |
[`DELETE`](dml_delete) | Delete specific rows from a table |
[`TRANSACTION`](dml_transaction) | Makes changes to multiple rows in one or more tables in a transaction |
[`TRUNCATE`](dml_truncate) | Remove all rows from a table |

## Expressions
An expression is a finite combination of one or more values, operators, functions, and expressions that specifies a computation. Expression can be used in the following components.

- The select list of [`SELECT`](dml_select) statement. For example, `SELECT id + 1 FROM sample_table;`.
- The WHERE clause in [`SELECT`](dml_select), [`DELETE`](dml_delete), [`INSERT`](dml_insert), or [`UPDATE`](dml_update).
- The IF clause in [`DELETE`](dml_delete), [`INSERT`](dml_insert), or [`UPDATE`](dml_update).
- The VALUES clause in [`INSERT`](dml_insert).
- The SET clause in [`UPDATE`](dml_update).

Currently, the following expressions are supported.

Expression | Description |
-----------|-------------|
[Simple Value](expr_simple) | Column, constant, or null. Column alias cannot be used in expression yet. |
[Subscript `[]`](expr_subscript) | Subscripting columns of collection datatypes |
[Operator Call](expr_ocall) | Builtin operators only |
[Function Call](expr_fcall) | Builtin function calls only |

## Data Types
- The following table lists all supported primitive types.

Primitive Type | Allowed in Key | Type Parameters | Description |
---------------|----------------|-----------------|-------------|
[`BIGINT`](type_int) | Yes | - | 64-bit signed integer |
[`BLOB`](type_blob) | Yes | - | String of binary characters |
[`BOOLEAN`](type_bool) | No | - | Boolean |
[`COUNTER`](type_int) | No | - | 64-bit signed integer |
[`DECIMAL`](type_number) | Yes | - | Exact, fixed-point number |
[`DOUBLE`](type_number) | Yes | - | 64-bit, inexact, floating-point number |
[`FLOAT`](type_number) | Yes | - | 64-bit, inexact, floating-point number |
[`FROZEN`](type_frozen) | Yes | 1 | Collection in binary format |
[`INET`](type_inet) | Yes | - | String representation of IP address |
[`INT` &#124; `INTEGER`](type_int) | Yes | - | 32-bit signed integer |
[`LIST`](type_collection) | No | 1 | Collection of ordered elements |
[`MAP`](type_collection) | No | 2 | Collection of pairs of key-and-value elements |
[`SET`](type_collection) | No | 1 | Collection of unique elements |
[`SMALLINT`](type_int) | Yes | - | 16-bit signed integer |
[`TEXT` &#124; `VARCHAR`](type_text) | Yes | - | String of Unicode characters |
[`TIMESTAMP`](type_timestamp) | Yes | - | Date-and-time |
[`TIMEUUID`](type_uuid) | Yes | - | Timed UUID |
[`TINYINT`](type_int) | Yes | - | 8-bit signed integer |
[`UUID`](type_uuid) | Yes | - | Standard UUID |
[`JSONB`](type_jsonb) | No | - | Json datatype similar to postgresql jsonb |

- [User-defined datatypes](ddl_create_type) are also supported.

- The following table lists all CQL primitive types that are not yet implemented.

Primitive Type |
---------------|
`DATE` | 
`TIME` | 
`TUPLE` | 
`VARINT` | 
