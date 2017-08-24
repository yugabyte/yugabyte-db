---
title: Apache Cassandra Query Language (CQL)
summary: CQL introduction and features.
---
<style>
table {
  float: left;
}
</style>

## Introduction

## Statements
YQL supports the following types of statements that are simimlar to Apache CQL and PostgreSQL.
<li> Data definition language (DDL) statements </li>
<li> Data manipulation language (DML) statements  </li>
<li> Transaction control statements </li>

<h2>DDL Statements</h2>
Data definition language (DDL) statements are instructions for the following database operations.
<li> Create, alter, and drop database objects </li>
<li> Create, grant, and revoke users and roles </li>

Statement | Description
----------|------------|
[`ALTER TABLE`](/yql/ql/alter-table) | Alter a table.
[`CREATE KEYSPACE`](/yql/ql/create-database) | Create a new keyspace.
[`CREATE TABLE`](/yql/ql/create-table) | Create a new table.
[`DROP KEYSPACE`](/yql/ql/drop-database) | Delete a keyspace and associated objects.
[`DROP TABLE`](/yql/ql/drop-table) | Remove a table.

Need to run "cqlsh" and check those statements that show the metadata.

<h2>DML Statements</h2>
Data manipulation language (DML) statements are to read from and write to the existing database objects. Similar to Apache CQL bebhavior, YQL implicitly commits any updates by DML statements.

Statement | Description
----------|-------------|
[`DELETE`](/yql/ql/delete) | Delete specific rows from a table.
[`INSERT`](/yql/ql/insert) | Insert rows into a table.
[`SELECT`](/yql/ql/select) | Select rows from a table.
[`TRUNCATE`](/yql/ql/truncate) | Deletes all rows from specified tables.
[`UPDATE`](/yql/ql/update) | Update rows in a table.

<h2>Transaction Control Statements</h2>
Transaction control statements are under development.

## [DataTypes](/yql/ql/datatypes)

## Data Constraints
