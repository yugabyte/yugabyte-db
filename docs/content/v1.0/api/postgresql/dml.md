---
title: DML Statements
description: PostgreSQL DML Statements
summary: DML overview and specification.
menu:
  v1.0:
    identifier: api-postgresql-dml
    parent: api-postgresql
    weight: 3200
aliases:
  - api/postgresql/dml
  - api/pgsql/dml
---

Data manipulation language (DML) statements are used to read from and write to the existing database objects. Currently, YugaByte DB implicitly commits any updates by DML statements.

Statement | Description |
----------|-------------|
[`INSERT`](../dml_insert) | Insert rows into a table |
[`SELECT`](../dml_select) | Select rows from a table |
[`UPDATE`](../dml_update) | Will be added. Update rows in a table |
[`DELETE`](../dml_delete) | Will be added. Delete rows from a table |
