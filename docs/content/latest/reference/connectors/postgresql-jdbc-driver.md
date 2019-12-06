---
title: PostgreSQL JDBC Driver
linkTitle: PostgreSQL JDBC Driver
description: PostgreSQL JDBC Driver
section: REFERENCE
menu:
  latest:
    identifier: postgresql-jdbc-driver
    parent: connectors
    weight: 2910
---

Because YugabyteDB is PostgreSQL-compatible, you can use the [PostgreSQL JDBC Driver](https://jdbc.postgresql.org/) with your favorite PostgreSQL tools and clients to develop and manage YugabyteDB databases.

## Get the PostgreSQL JDBC Driver

To get the latest PostgreSQL JDBC Driver, go the the [PostgreSQL JDBC Driver download page](https://jdbc.postgresql.org/download.html). For more information on the PostgreSQL JDBC driver, see the [PostgreSQL JDBC Driver documentation](https://jdbc.postgresql.org/documentation/documentation.html).

## Use the PostgreSQL JDBC Driver with popular third party tools

Because YugabyteDB is PostgreSQL-compatible, you can use the PostgreSQL JDBC Driver with popular third party tools that support PostgreSQL. When using the PostgreSQL JDBC Driver with YugabyteDB databases, remember to use YugabyteDB's default port of `5433` (instead of PostgreSQL's default of `5432`) and use the default YugabyteDB user `yugabyte` instead of the PostgreSQL default user (`postgres`).

You can get started by using our tutorials on popular [third party tools](../../../tools/) that use the PostgreSQL JDBC Driver to develop and manage YugabyteDB databases.

- [DBeaver](../tools/dbeaver/)
- [pgAdmin](../tools/pgadmin/)
- [SQL Workbench/J](../tools/sql-workbench/)
- [Table Plus](../tools/tableplus/)
