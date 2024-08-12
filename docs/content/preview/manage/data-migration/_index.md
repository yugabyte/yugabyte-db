---
title: Migrate data
headerTitle: Migrate data
linkTitle: Migrate
description: Import and export data in YugabyteDB.
image: fa-thin fa-cloud-binary
headcontent: Export data and schema from other databases and import into YugabyteDB
menu:
  preview:
    identifier: manage-bulk-import-export
    parent: manage
    weight: 703
type: indexpage
---

Migrating to YugabyteDB, a high-performance distributed SQL database, involves a series of carefully planned steps to ensure a seamless transition from your existing database environment, such as PostgreSQL, to a scalable, fault-tolerant, and globally distributed system. Whether you're moving from a monolithic setup or another distributed system, understanding the migration strategies, tools, and best practices is crucial to achieving a successful migration.

## YugabyteDB Voyager

Yugabyte ships [YB Voyager](../../yugabyte-voyager/), a comprehensive data migration tool designed specifically to help users migrate from traditional databases like PostgreSQL, Oracle, MySQL, and others to YugabyteDB. It offers a variety of features that make the migration process smoother, more reliable, and less error-prone.

{{<lead link="../../yugabyte-voyager/migrate/">}}
To learn more about how to methodically export using YB Voyager, see [Migrate](../../yugabyte-voyager/migrate/)
{{</lead>}}

## Export your data

In case you have the need to export your data manually, you can use [ysql_dump](../../admin/ysql-dump/) tool or use the [COPY TO](../../api/ysql/the-sql-language/statements/cmd_copy/) command to export your tables into csv files.

{{<lead link="../../manage/data-migration/bulk-export-ysql/">}}
To learn more about how to use `ysql_dump` utility and `COPY` command to export your data, see [Data export](../../manage/data-migration/bulk-export-ysql/)
{{</lead>}}

## Import your data

To import data into a YSQL database manually, you can use [ysqlsh](../../admin/ysqlsh/) tool or use the [COPY FROM](../../api/ysql/the-sql-language/statements/cmd_copy/) command to import csv files into YugabyteDB.

{{<lead link="../../manage/data-migration/bulk-import-ysql/">}}
To learn more about how to import your data manually and the best practices that should be followed, see [Data import](../../manage/data-migration/bulk-import-ysql/)
{{</lead>}}

## Verify migration

Once the data has been imported into the newly setup YugabyteDB cluster, it is of paramount importance to verify and validate that the data and the schema have been migrated correctly to ensure smooth functioning of your services.

{{<lead link="">}}
To understand the various steps involved in validating your data, see [Verify migration](./verify-migration-ysql)
{{</lead>}}

## Guide to migration from PostgreSQL

Migration of data from one system to another is no simple task. It involves pre-planning, exporting/importing schema, analyzing and modifying schema, exporting/importing data and so on. We have come up with a set of precautions and steps to guide you through the journey to ensure a smooth migration.

{{<lead link="../../manage/data-migration/migrate-from-postgres/">}}
To understand the various steps involved in migration from PostgreSQL and the best practices to be followed, see [Migrate from PostgreSQL](../../manage/data-migration/migrate-from-postgres/)
{{</lead>}}