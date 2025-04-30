---
title: Behavior of DDL statements [YSQL]
headerTitle: Behavior of DDL statements
linkTitle: Behavior of DDL statements
description: Explains how the behavior of DDL statements works in YugabyteDB YSQL and documents differences from Postgres behavior. [YSQL].
menu:
  stable_api:
    identifier: ddl-behavior
    parent: api-ysql
    weight: 20
type: docs
---


This section describes how DDL statements work in YSQL and documents the difference in YugabyteDB behavior from PostgreSQL.

## Concurrent DML during a DDL operation

In YugabyteDB, DML is allowed to execute while a DDL statement modifies the schema that is accessed by the DML statement. For example, an `ALTER TABLE <table> .. ADD COLUMN` DDL statement may add a new column while a `SELECT * from <table>` executes concurrently on the same relation. In PostgreSQL, this is typically not allowed because such DDL statements take a table-level exclusive lock that prevents concurrent DML from executing (support for similar behavior in YugabyteDB is being tracked in [github issue])

In YugabyteDB, DML that run concurrently with a DDL may see one of the following results:
1. Operate with the old schema prior to the DDL.
2. Operate with the new schema after the DDL completes.
3. Encounter temporary errors such as `schema mismatch errors` or `catalog version mismatch`. It is recommended for the client to retry such operations whenever possible.

Most DDL statements complete quickly, so this is typically not a significant issue in practice. However, [certain kinds of ALTER TABLE DDL statements](/the-sql-language/statements/ddl_alter_table.md/#alter-type-with-table-rewrite) involve making a full copy of the table(s) whose schema is being modified. For these operations, it is not recommended to run any concurrent DML statements during the `ALTER TABLE` as the effect of the DML may not be reflected in the copied table after the DDL is complete.

## Concurrent DDL during a DDL operation

TODO: add details
