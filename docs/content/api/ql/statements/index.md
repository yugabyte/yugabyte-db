---
title: Query Language Statements
toc: false
---
<style>
table {
      float: left;
}
</style>

YQL supports the following types of statements that are simimlar to Apache CQL and PostgreSQL.
<li> Data definition language (DDL) statements </li>
<li> Data manipulation language (DML) statements  </li>
<li> Transaction control statements </li>

## DDL
Data definition language (DDL) statements are instructions for the following database operations.
<li> Create, alter, and drop database objects </li>
<li> Create, grant, and revoke users and roles </li>

Statement | Description
----------|------------|
[`ALTER TABLE`](alter-table.html) | Alter a table.
[`CREATE KEYSPACE`](create-database.html) | Create a new keyspace.
[`CREATE TABLE`](create-table.html) | Create a new table.
[`DROP KEYSPACE`](drop-database.html) | Delete a keyspace and associated objects.
[`DROP TABLE`](drop-table.html) | Remove a table.

Need to run "cqlsh" and check those statements that show the metadata.

## DML
Data manipulation language (DML) statements are to read from and write to the existing database objects. Similar to Apache CQL bebhavior, YQL implicitly commits any updates by DML statements.

Statement | Description
----------|-------------|
[`DELETE`](delete.html) | Delete specific rows from a table.
[`INSERT`](insert.html) | Insert rows into a table.
[`SELECT`](select.html) | Select rows from a table.
[`TRUNCATE`](truncate.html) | Deletes all rows from specified tables.
[`UPDATE`](update.html) | Update rows in a table.

## Transaction
Transaction control statements are under development.
