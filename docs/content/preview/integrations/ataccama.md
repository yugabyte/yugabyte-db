---
title: Ataccama DQ Analyzer
linkTitle: Ataccama DQ Analyzer
description: Use Ataccama DQ Analyzer with YSQL API
menu:
  preview_integrations:
    identifier: ataccama
    parent: data-discovery
    weight: 571
type: docs
---

[DQ Analyzer](https://support.ataccama.com/home/docs/dqa/introduction-to-dqa) combines advanced data profiling and analysis capabilities with a point-and-click interface that is basic enough for business managers to use without extensive training. All Ataccama solutions use databases in one way or another. They can be used as data sources and repositories for storing data.

Because YugabyteDB's YSQL API is wire-compatible with PostgreSQL, DQ Analyzer can connect to YugabyteDB as a data source using the PostgreSQL JDBC driver which is shipped with the analyzer itself.

## Connect

Your YugabyteDB cluster should be up and running. Refer to [YugabyteDB Prerequisites](../../tools/#yugabytedb-prerequisites).

Follow the steps in [Ataccama documentation](https://support.ataccama.com/home/docs/dqa/user-guide/working-with-databases) to connect the database.

Perform the following modifications as part of the connection steps:

- Select PostgreSQL as the database type but provide the configuration to connect to YugabyteDB. DQ Analyzer can connect to YugabyteDB considering it as PostgreSQL.

- Change the port from `5432` to `5433`.
