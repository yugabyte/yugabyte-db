---
title: Transactional DDL
headerTitle: Transactional DDL
linkTitle: Transactional DDL
description: How YugabyteDB rolls back DDL statements that run inside a transaction block.
tags:
  feature: early-access
menu:
  stable:
    identifier: architecture-transactional-ddl
    parent: architecture-acid-transactions
    weight: 305
type: docs
---

Data Definition Language (DDL) statements modify the structure of your data. PostgreSQL allows executing such statements within a transactional block and supports rolling them back as part of a transaction rollback. For two transactions undergoing conflicting DDLs, it also provides isolation guarantees by taking appropriate object locks during DDL processing.

YugabyteDB's transactional DDL provides similar guarantees for rolling back DDL operations done inside a transaction block. Because object locks are still under active development, isolation guarantees are weaker than in PostgreSQL. See [Limitations](#limitations).

{{<lead link="../../../explore/transactions/transactional-ddl/">}}
To try rolling back a DDL statement, see [Transactional DDL](../../../explore/transactions/transactional-ddl/).
{{</lead>}}

## Enable transactional DDL

{{<tags/feature/ea idea="1677">}} Support for transactional DDL is disabled by default. To enable the feature, set the [yb-tserver](../../../reference/configuration/yb-tserver/) flag `ysql_yb_ddl_transaction_block_enabled` to true.

{{< warning title="Warning" >}}

Do not enable transactional DDL if you are using CDC. Transactional DDL currently doesn't support CDC in both [logical replication](../../docdb-replication/cdc-logical-replication/) (PostgreSQL) and the [gRPC protocol](../../docdb-replication/change-data-capture/). See [Limitations](#limitations).

{{< /warning >}}

## Rollback capabilities

All DDLs supported in YugabyteDB provide the same rollback capabilities as PostgreSQL. These include DDLs on tables, indexes, roles, and materialized views.

Some DDL statements, such as DDLs on a database or tablespace, are disallowed in a transaction block in PostgreSQL, and are also disallowed in YugabyteDB.

## Limitations

- [Concurrent DDLs](../../../best-practices-operations/administration/#concurrent-ddl-during-a-ddl-operation) on the same database are unsupported and will lead to conflict and read restart required errors. Your applications must handle these by retrying the statements.

- [Savepoints](/stable/develop/learn/transactions/transactions-retries-ysql/#savepoints) are unsupported for DDL statements. As a result, you cannot create a savepoint in a transaction block that has executed a DDL statement. Similarly, you cannot execute a DDL statement in a transaction block in which a savepoint has been created.

- Transactional DDL currently doesn't support CDC in both [logical replication](../../docdb-replication/cdc-logical-replication/) (PostgreSQL) and the [gRPC protocol](../../docdb-replication/change-data-capture/). You must not enable transactional DDL if you are using CDC.
