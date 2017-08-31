---
title: Apache Cassandra Query Language (CQL)
summary: CQL features.
---
<style>
table {
  float: left;
}
</style>

## Introduction
YugaByte supports the following Apache Cassandra features.
<li> All primitive datatypes</li>
<li> Data definition language (DDL) statements</li>
<li> Data manipulation language (DML) statements</li>
<li> Transaction control statements</li>

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

## Transaction Control Statements
Transaction control statements are under development.

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
All primitive datatypes in Apache Cassandra are supported.

Type | Description |
-----|-------------|
[`BIGINT`](type_int) | 64-bit signed integer |
[`BLOB`](type_blob) | A string of binary characters |
[`BOOL`](type_bool) | A Boolean value |
[`DATE`](type_date) | A date |
[`DECIMAL`](type_decimal) | An exact, fixed-point number |
[`DOUBLE`](type_float) | A 64-bit, inexact, floating-point number |
[`FLOAT`](type_float) | A 64-bit, inexact, floating-point number |
[<code>INT &#124; INTEGER</code>](type_int) | 32-bit signed integer |
[`INTERVAL`](type_interval) | A span of time |
[`SMALLINT`](type_int) | 16-bit signed integer |
[`TEXT`](type_text) | A string of Unicode characters |
[`TIMESTAMP`](type_timestamp) | A date and time pairing |
[`TINYINT`](type_int) | 8-bit signed integer |
