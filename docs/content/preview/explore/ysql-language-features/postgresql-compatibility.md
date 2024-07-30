---
title: PostgreSQL compatibility
linkTitle: PostgreSQL compatibility
description: Summary of YugabyteDB's PostgreSQL compatibility
aliases:
  - /preview/ysql/postgresql-compatibility/
  - /preview/explore/ysql-language-features/postgresql-compatibility/
menu:
  preview:
    identifier: explore-ysql-postgresql-compatibility
    parent: explore-ysql-language-features
    weight: 70
type: docs
---

YugabyteDB is a [PostgreSQL-compatible](https://www.yugabyte.com/tech/postgres-compatibility/) distributed database that supports the majority of PostgreSQL syntax. This means that existing applications built on PostgreSQL can often be migrated to YugabyteDB without changing application code.

Because YugabyteDB is PostgreSQL compatible, it works with the majority of PostgreSQL database tools such as various language drivers, ORM tools, schema migration tools, and many more third-party database tools.

Because YugabyteDB is a distributed database, supporting all PostgreSQL features easily in a distributed system is not always feasible. This page documents the known list of differences between PostgreSQL and YugabyteDB. You need to consider these differences while porting an existing application to YugabyteDB.

## Unsupported PostgreSQL features

The following PostgreSQL features are not supported in YugabyteDB:

| Unsupported PostgreSQL feature      | Track feature request GitHub issue |
| ----------- | ----------- |
| LOCK TABLE to obtain a table-level lock | [5384](https://github.com/yugabyte/yugabyte-db/issues/5384)|
| Table inheritance    | [5956](https://github.com/yugabyte/yugabyte-db/issues/5956)|
| Exclusion constraints | [3944](https://github.com/yugabyte/yugabyte-db/issues/3944)|
| Deferrable constraints | [1709](https://github.com/yugabyte/yugabyte-db/issues/1709)|
| GiST indexes | [1337](https://github.com/yugabyte/yugabyte-db/issues/1337)|
| Events (Listen/Notify) | [1872](https://github.com/yugabyte/yugabyte-db/issues/1872)|
| XML Functions | [1043](https://github.com/yugabyte/yugabyte-db/issues/1043)|
| XA syntax | [11084](https://github.com/yugabyte/yugabyte-db/issues/11084)|
| ALTER TYPE | [1893](https://github.com/yugabyte/yugabyte-db/issues/1893)|
| CREATE CONVERSION | [10866](https://github.com/yugabyte/yugabyte-db/issues/10866)|
| Primary/Foreign key constraints on foreign tables | [10698](https://github.com/yugabyte/yugabyte-db/issues/10698), [10699](https://github.com/yugabyte/yugabyte-db/issues/10699) |
| GENERATED ALWAYS AS STORED columns | [10695](https://github.com/yugabyte/yugabyte-db/issues/10695)|
| Multicolumn GIN indexes| [10652](https://github.com/yugabyte/yugabyte-db/issues/10652)|
| CREATE ACCESS METHOD | [10693](https://github.com/yugabyte/yugabyte-db/issues/10693)|
| DESC/HASH on GIN indexes (ASC supported) | [10653](https://github.com/yugabyte/yugabyte-db/issues/10653)|
| CREATE SCHEMA with elements | [10865](https://github.com/yugabyte/yugabyte-db/issues/10865)|
| Index on citext column | [9698](https://github.com/yugabyte/yugabyte-db/issues/9698)|
| ABSTIME type | [15637](https://github.com/yugabyte/yugabyte-db/issues/15637)|
| transaction ids (xid) <br/> YugabyteDB uses [Hybrid logical clocks](../../../architecture/transactions/transactions-overview/#hybrid-logical-clocks) instead of transaction ids. | [15638](https://github.com/yugabyte/yugabyte-db/issues/15638)|
| DDL operations within transaction| [1404](https://github.com/yugabyte/yugabyte-db/issues/1404)|
| Some ALTER TABLE variants| [1124](https://github.com/yugabyte/yugabyte-db/issues/1124)|
