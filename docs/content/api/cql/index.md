---
title: Apache Cassandra Query Language (CQL)
summary: CQL features.
---
<style>
table {
  float: left;
}
#psyn {
  text-indent: 50px;
}
</style>

## Introduction
YugaByte supports the following Apache Cassandra features.
<li> Data definition language (DDL) statements.</li>
<li> Data manipulation language (DML) statements.</li>
<li> Builtin functions and Expression operators.</li>
<li> Primitive user-defined datatypes.</li>

## DDL Statements
Data definition language (DDL) statements are instructions for the following database operations.
<li> Create, alter, and drop database objects</li>
<li> Create, grant, and revoke users and roles</li>

Statement | Description |
----------|-------------|
[`ALTER TABLE`](ddl_alter_table) | Alter a table |
[`CREATE KEYSPACE`](ddl_create_keyspace) | Create a new keyspace |
[`CREATE TABLE`](ddl_create_table) | Create a new table |
[`CREATE TYPE`](ddl_create_type) | Construct a user-defined datatype |
[`DROP KEYSPACE`](ddl_drop_keyspace) | Delete a keyspace and associated objects |
[`DROP TABLE`](ddl_drop_table) | Remove a table |
[`DROP TYPE`](ddl_drop_type) | Remove a user-defined datatype |
[`USE`](ddl_use) | Use an existing keyspace for subsequent commands|

## DML Statements
Data manipulation language (DML) statements are to read from and write to the existing database objects. Similar to Apache Cassandra bebhavior, YugaByte implicitly commits any updates by DML statements.

Statement | Description |
----------|-------------|
[`DELETE`](dml_delete) | Delete specific rows from a table.
[`INSERT`](dml_insert) | Insert rows into a table.
[`SELECT`](dml_select) | Select rows from a table.
[`UPDATE`](dml_update) | Update rows in a table.
[`TRUNCATE`](.) | Not yet supported.

## Expressions
An expression is a finite combination of one or more values, operators, functions, and expressions that specifies a computation. Expression can be used in the following components.
<li>The select list of [`SELECT`](dml_select) statement. For example, `SELECT id + 1 FROM yugatab;`.</li>
<li>The WHERE clause in [`SELECT`](dml_select), [`DELETE`](dml_delete), [`INSERT`](dml_insert), or [`UPDATE`](dml_update).</li>
<li>The IF clause in [`DELETE`](dml_delete), [`INSERT`](dml_insert), or [`UPDATE`](dml_update).</li>
<li>The VALUES clause in [`INSERT`](dml_insert).</li>
<li>The SET clause in [`UPDATE`](dml_update).</li>

Currently, the following expressions are supported.

Expression | Description |
-----------|-------------|
[Simple Value](expr_simple) | Column, constant, or null. Column alias cannot be used in expression yet. |
[Subscript `[]`](expr_subscript) | Subscripting columns of collection datatypes |
[Operator Call](expr_ocall) | Builtin operators only |
[Function Call](expr_fcall) | Builtin function calls only |

## DataTypes
<li> [User-defined datatypes](ddl_create_type) in Apache Cassandra are supported.</li>
<li> The following table lists all supported primitive types.

Primitive Type | Description |
---------------|-------------|
[`BIGINT`](type_int) | 64-bit signed integer |
[`BLOB`](type_blob) | String of binary characters |
[`BOOLEAN`](type_bool) | boolean |
[`COUNTER`](type_int) | 64-bit signed integer |
[`DECIMAL`](type_number) | Exact, fixed-point number |
[`DOUBLE`](type_number) | 64-bit, inexact, floating-point number |
[`FLOAT`](type_number) | 64-bit, inexact, floating-point number |
[`FROZEN`](type_frozen) | Collection in binary format |
[`INET`](type_inet) | String representation of IP address |
[`INT` &#124; `INTEGER`](type_int) | 32-bit signed integer |
[`LIST`](type_collection) | Collection of ordered elements |
[`MAP`](type_collection) | Collection of pairs of key-and-value elements |
[`SET`](type_collection) | Collection of ordered elements |
[`SMALLINT`](type_int) | 16-bit signed integer |
[`TEXT`](type_text) | String of Unicode characters |
[`TIMESTAMP`](type_timestamp) | Date-and-time |
[`TIMEUUID`](type_uuid) | Timed UUID |
[`TINYINT`](type_int) | 8-bit signed integer |
[`UUID`](type_uuid) | Standard UUID |
</li>

<li> The following table lists all primitive types that are not yet implemented.

Primitive Type |
---------------|
`DATE` | 
`TIME` | 
`TUPLE` | 
`VARINT` | 
</li>
