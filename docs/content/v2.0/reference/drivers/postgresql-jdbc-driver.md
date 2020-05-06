---
title: PostgreSQL JDBC Driver
linkTitle: PostgreSQL JDBC Driver
description: PostgreSQL JDBC Driver
section: REFERENCE
block_indexing: true
menu:
  v2.0:
    identifier: postgresql-jdbc-driver
    parent: drivers
    weight: 2910
isTocNested: 3
showAsideToc: true
---

Because YSQL is PostgreSQL compatible, you can use the [PostgreSQL JDBC Driver](https://jdbc.postgresql.org/) with your favorite PostgreSQL tools and clients to develop and manage YugabyteDB.

## Download

To get the latest PostgreSQL JDBC Driver, go the the [PostgreSQL JDBC Driver download page](https://jdbc.postgresql.org/download.html). For more information on the PostgreSQL JDBC driver, see the [PostgreSQL JDBC Driver documentation](https://jdbc.postgresql.org/documentation/documentation.html).

## Use with popular third party tools

When using the PostgreSQL JDBC Driver with YugabyteDB, remember to use YugabyteDB's default port of `5433` (instead of PostgreSQL's default of `5432`) and use the default YugabyteDB user `yugabyte` instead of the PostgreSQL default user (`postgres`).

You can get started by using our tutorials on popular [third party tools](../../../tools/) that use the PostgreSQL JDBC Driver to develop and manage YugabyteDB databases.

- [DBeaver](../../../tools/dbeaver/)
- [pgAdmin](../../../tools/pgadmin/)
- [SQL Workbench/J](../../../tools/sql-workbench/)
- [Table Plus](../../../tools/tableplus/)
