---
title: Migrate data
headerTitle: Migrate data
linkTitle: Migrate
description: Import and export data in YugabyteDB.
image: fa-thin fa-cloud-binary
headcontent: Export data and schema from other databases and import into YugabyteDB
menu:
  stable:
    identifier: manage-bulk-import-export
    parent: manage
    weight: 703
type: indexpage
---

Migrating to YugabyteDB, a high-performance distributed SQL database, involves a series of carefully planned steps to ensure a seamless transition from your existing database environment, such as PostgreSQL, to a scalable, fault-tolerant, and globally distributed system. Whether you're moving from a monolithic setup or another distributed system, understanding the migration strategies, tools, and best practices is crucial to achieving a successful migration.

## YugabyteDB Voyager

Yugabyte ships [YugabyteDB Voyager](/preview/yugabyte-voyager/), a comprehensive data migration tool designed specifically to help users migrate from traditional databases like PostgreSQL, Oracle, MySQL, and others to YugabyteDB. It offers a variety of features that make the migration process smoother, more reliable, and less error-prone.

{{<lead link="/preview/yugabyte-voyager/migrate/">}}
To learn more about how to methodically export using YB Voyager, see [Migrate](/preview/yugabyte-voyager/migrate/).
{{</lead>}}

## Export your data

If you need to export your data manually, you can use the [ysql_dump](../../admin/ysql-dump/) tool or use the [COPY TO](../../api/ysql/the-sql-language/statements/cmd_copy/) command to export your tables into CSV files.

{{<lead link="../../manage/data-migration/bulk-export-ysql/">}}
To learn more about how to use ysql_dump and the `COPY` command to export your data, see [Export data](../../manage/data-migration/bulk-export-ysql/).
{{</lead>}}

## Import your data

To import data into a YSQL database manually, you can use [ysqlsh](../../admin/ysqlsh/) tool or use the [COPY FROM](../../api/ysql/the-sql-language/statements/cmd_copy/) command to import CSV files into YugabyteDB.

{{<lead link="../../manage/data-migration/bulk-import-ysql/">}}
To learn more about how to import your data manually and best practices, see [Import data](../../manage/data-migration/bulk-import-ysql/).
{{</lead>}}

## Verify migration

After the data has been imported into the newly set up YugabyteDB cluster, you need to verify and validate that the data and the schema have been migrated correctly to ensure smooth functioning of your services.

{{<lead link="">}}
To understand the various steps involved in validating your data, see [Verify migration](./verify-migration-ysql).
{{</lead>}}

## Guide to migration from PostgreSQL

Migrating from one system to another is no trivial task. It involves pre-planning, exporting and importing schema, analyzing and modifying schema, exporting and importing data, and more. Follow these precautions and steps to ensure a smooth migration.

{{<lead link="../../manage/data-migration/migrate-from-postgres/">}}
To understand the various steps involved in migrating from PostgreSQL and best practices, see [Migrate from PostgreSQL](../../manage/data-migration/migrate-from-postgres/).
{{</lead>}}