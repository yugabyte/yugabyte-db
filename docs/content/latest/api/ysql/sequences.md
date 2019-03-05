---
title: Sequences Statements
description: PostgreSQL Sequences Statements
summary: Sequences overview and specification.
menu:
  latest:
    identifier: api-postgresql-sequences
    parent: api-postgresql
    weight: 3300
aliases:
  - /latest/api/postgresql/sequences
  - /latest/api/ysql/sequences
isTocNested: true
showAsideToc: true
---

Statements and functions to create and obtain information from sequences.

## Sequences
Statement | Description |
----------|------------|
[`CREATE SEQUENCE`](../create_sequence) | Create a new sequence |
[`DROP SEQUENCE`](drop_sequence) | Drop a sequence |
[`nextval(sequence)`](../nextval_sequence) | Get the next value in the sequence
[`currval(sequence)`](../currval_sequence) | Get the last value returned by the most recent nextval call for the specified sequence
[`lastval()`](../lastval_sequence) | Get the last value returned by the most recent nextval call for any sequence

- `ALTER SEQUENCE` and  `setval()` are not supported yet.
