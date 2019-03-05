---
title: DML Statements
description: PostgreSQL DML Statements
summary: DML overview and specification.
menu:
  v1.1:
    identifier: api-postgresql-dml
    parent: api-postgresql
    weight: 3200
isTocNested: true
showAsideToc: true
---

Data manipulation language (DML) statements are used to read from and write to the existing database objects. Currently, YugaByte DB implicitly commits any updates by DML statements.

Statement | Description |
----------|-------------|
[`INSERT`](../dml_insert) | Insert rows into a table |
[`SELECT`](../dml_select) | Select rows from a table |
[`UPDATE`] | In progress. Update rows in a table |
[`DELETE`] | In progress. Delete rows from a table |
