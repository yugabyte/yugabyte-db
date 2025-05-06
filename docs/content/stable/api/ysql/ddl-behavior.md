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

In YugabyteDB, when a DDL modifies the schema of tables that are accessed by concurrent DML statements, the DML statement may 
1. Operate with the old schema prior to the DDL, or
2. Operate with the new schema after the DDL completes, or
3. Encounter temporary errors such as `schema mismatch errors` or `catalog version mismatch`. It is recommended for the client to [retry such operations](https://www.yugabyte.com/blog/retry-mechanism-spring-boot-app/) whenever possible.

Most DDL statements complete quickly, so this is typically not a significant issue in practice. However, [certain kinds of ALTER TABLE DDL statements](../the-sql-language/statements/ddl_alter_table/#alter-table-operations-that-involve-a-table-rewrite) involve making a full copy of the table(s) whose schema is being modified. For these operations, it is not recommended to run any concurrent DML statements on the table being modified by the `ALTER TABLE` as the effect of such concurrent DML may not be reflected in the table copy.

## Concurrent DDL during a DDL operation

DDL statements that affect entities in different databases can be run concurrently. However, you cannot concurrently execute two DDL statements that affect entities in the same database. DDL that relate to shared objects like roles, tablespaces are considered as affecting all databases in the cluster, so they cannot be run concurrently with any per-database DDL. 

