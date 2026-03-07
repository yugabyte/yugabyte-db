---
title: YugabyteDB Voyager introduction
headerTitle: Introduction
linkTitle: Introduction
headcontent: Migrate your database using YugabyteDB Voyager
cascade:
  unversioned: true
description: Migrate your database to YugabyteDB using YugabyteDB Voyager.
type: docs
showRightNav: true
menu:
  stable_yugabyte-voyager:
    identifier: overview-vgr
    parent: yugabytedb-voyager
    weight: 100
---

YugabyteDB Voyager is a powerful open-source data migration engine that accelerates cloud native adoption by removing barriers to moving applications to the public or private cloud. It helps you migrate databases to YugabyteDB quickly and securely.

![Voyager Architecture](/images/migrate/voyager-architecture2.png)

You manage the entire lifecycle of a database migration, including cluster preparation for data import, schema migration, and data migration, using the [yb-voyager](https://github.com/yugabyte/yb-voyager) command line interface (CLI).

## Features

YugabyteDB Voyager has the following features:

- Free and completely open source.
- Supports widely-used databases for migration and, in most cases, doesn't require changes to the [source databases](#source-databases).
- Supports all YugabyteDB products as the [target database](#target-database).
- [Unified CLI](../reference/yb-voyager-cli/) experience for all different source databases.
- [Auto-tuneable](../reference/performance/) based on workloads, by analyzing the target cluster capacity; runs parallel jobs by default.
- Progress monitoring, including the expected time for data export and import to complete, using progress bars.
- In case of failures, data import can be resumed.
- Parallelism of data across tables.
- Support for direct data import from CSV or TEXT format files present on local disk or on any cloud storage.
- Live migration of Oracle databases with fall-forward and fall-back. {{<tags/feature/tp>}}
- Live migration of PostgreSQL databases with fall-forward and fall-back. {{<tags/feature/ga>}}

## Migration types

You can perform migration by choosing one of the following options:

- [Offline migration](../migrate/migrate-steps/) - Take your applications offline to perform the migration.
- [Live migration](../migrate/live-migrate/) - Migrate your data while your application is running (currently Oracle and PostgreSQL only).
- [Live migration with fall-forward](../migrate/live-fall-forward/) - Fall forward to the source-replica database for your live migration (currently Oracle and PostgreSQL only).
- [Live migration with fall-back](../migrate/live-fall-back/) - Fall back to the source database for your live migration (currently Oracle and PostgreSQL only).

## Source databases

YugabyteDB Voyager supports migrating schema and data from your existing RDBMS, as described in the following table:

| Source&nbsp;database&nbsp;type | Migration&nbsp;type | Supported&nbsp;versions&nbsp;and&nbsp;flavors | Migration&nbsp;demo&nbsp;videos |
| :--------------------| :------------- |:----------------------------------- | :--------------- |
| PostgreSQL | Offline and Live | PostgreSQL 11.x - 17.x <br> [Amazon Aurora PostgreSQL](https://docs.aws.amazon.com/AmazonRDS/latest/AuroraUserGuide/Aurora.AuroraPostgreSQL.html) <br> [Amazon RDS for PostgreSQL](https://aws.amazon.com/rds/postgresql/) <br> [Cloud SQL for PostgreSQL](https://cloud.google.com/sql/docs/postgres) <br> [Azure&nbsp;Database&nbsp;for&nbsp;PostgreSQL](https://azure.microsoft.com/en-ca/services/postgresql/) | [Migrating from PostgreSQL to YugabyteDB](https://www.youtube.com/watch?v=GXjttCbc4dw) |
| MySQL | Offline | MySQL 8.x <br> MariaDB <br> [Amazon Aurora MySQL](https://docs.aws.amazon.com/AmazonRDS/latest/AuroraUserGuide/Aurora.AuroraMySQL.html) <br> [Amazon RDS for MySQL](https://aws.amazon.com/rds/mysql/) <br> [Cloud SQL for MySQL](https://cloud.google.com/sql/docs/mysql) | [Migrating from MySQL to YugabyteDB](https://www.youtube.com/watch?v=tLs0043_z5E) |
| Oracle | Offline and Live |Oracle 11g - 19c <br> [Amazon RDS for Oracle](https://aws.amazon.com/rds/oracle/) | [Migrating from Oracle to YugabyteDB](https://www.youtube.com/watch?v=Bh2Wz537TGM) |

## Target database

The following table shows the target database support for each migration type.

| Migration type | Supported YugabyteDB Versions | Supported products |
| :------------- | :--------------------------- | ------------------ |
| Offline | {{<release "2.20.1.0">}} or later <br> {{<release "2.21.0.0">}} or later <br> {{<release "2024.1.1.0">}} or later  <br> {{<release "2025.1.0.0">}} or later <br> {{<release "2025.2.0.0">}} or later| [YugabyteDB](../../deploy/)<br>[YugabyteDB Anywhere](../../yugabyte-platform/create-deployments/)<br>[YugabyteDB Aeon](../../yugabyte-cloud/cloud-basics/) |
| Live | {{<release "2.20.3.0">}} or later <br>{{<release "2.21.1.0">}} or later  <br> {{<release "2024.1.1.0">}} or later <br> {{<release "2025.1.0.0">}} or later <br> {{<release "2025.2.0.0">}} or later | [YugabyteDB](../../deploy/)<br>[YugabyteDB Anywhere](../../yugabyte-platform/create-deployments/)<br>[YugabyteDB Aeon](../../yugabyte-cloud/cloud-basics/) |
| Live with fall-forward/fall-back <br> with [YugabyteDB gRPC Connector](../../additional-features/change-data-capture/using-yugabytedb-grpc-replication/debezium-connector-yugabytedb/)| {{<release "2.20.3.0">}} or later  <br> {{<release "2024.1.1.0">}} or later <br> {{<release "2025.1.0.0">}} or later <br> {{<release "2025.2.0.0">}} or later| [YugabyteDB](../../deploy/)<br>[YugabyteDB Anywhere](../../yugabyte-platform/create-deployments/) |
| Live with fall-forward/fall-back <br> with [YugabyteDB Connector](../../additional-features/change-data-capture/using-logical-replication/yugabytedb-connector/)| {{<release "2024.1.1.0">}} or later <br> {{<release "2025.1.0.0">}} or later <br> {{<release "2025.2.0.0">}} or later| [YugabyteDB](../../deploy/)<br>[YugabyteDB Anywhere](../../yugabyte-platform/create-deployments/)<br>[YugabyteDB Aeon](../../yugabyte-cloud/cloud-basics/) |

## Learn more

- [How to Migrate from Oracle Database to YugabyteDB Using YugabyteDB Voyager](https://www.yugabyte.com/blog/migrate-from-oracle-database-to-yugabytedb-using-voyager/)
- [Migrating from MySQL to YugabyteDB Using YugabyteDB Voyager](https://www.yugabyte.com/blog/migrate-web-application-mysql-yugabytedb-voyager/)
- [Exploring Migration Options From PostgreSQL Using YugabyteDB Voyager](https://www.yugabyte.com/blog/postgresql-migration-options-using-yugabytedb-voyager/)
