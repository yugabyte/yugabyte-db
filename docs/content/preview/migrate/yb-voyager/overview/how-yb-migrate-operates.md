---
title: How YB Voyager works
linkTitle: How YB Voyager works
description: Overview of YB Voyager.
menu:
  preview:
    identifier: how-yb-voyager-works
    parent: overview
    weight: 301
isTocNested: true
showAsideToc: true
---

YB Voyager is an open-source database migration engine provided by YugabyteDB. The engine manages the entire lifecycle of a database migration including cluster preparation for data import, schema-migration and data-migration using [yb-voyager](https://github.com/yugabyte/yb-voyager).

## yb-voyager

yb-voyager is a command line executable program that supports migrating databases from PostgreSQL, Oracle, and MySQL to a YugabyteDB database. yb-voyager keeps all of its migration state, including exported schema and data, in a local directory called the *export directory*. For more information, refer to [Export directory](../../reference/connectors/yb-migration-reference/#export-directory) in the Reference section.

## Migration modes

| Mode |  Description |
| :------------- | :----------- |
| Offline | In this mode, the source database should not change during the migration.<br> The offline migration is considered complete when all the requested schema objects and data are migrated to the target database. |
| Online | In this mode, the source database can continue to change. After the full initial migration, yb-voyager continues replicating source database changes to the target database. <br> The process runs continuously till you decide to switch over to the YugabyteDB database. |

{{< note title="Note" >}}
yb-voyager supports only `offline` migration mode. The `online` migration mode is currently under development. For more details, refer to this [github issue](https://github.com/yugabyte/yb-voyager/issues/50).
{{< /note >}}

## Migration workflow

A typical migration workflow using yb-voyager consists of the following steps:

- [Install yb-voyager](#1-install-yb-migrate) on a *migrator machine*.
- Convert the source database schema to PostgreSQL format using the [`yb-voyager export schema`](#export-schema) command.
- Generate a *Schema Analysis Report* using the [`yb-voyager analyze-schema`](#analyze-schema) command. The report suggests changes to the PostgreSQL schema to make it appropriate for YugabyteDB.
- [Manually](#manually-edit-the-schema) change the exported schema as suggested in the Schema Analysis Report.
- Dump the source database in the local files on the migrator machine using the [`yb-voyager export data`](#step-4-export-data) command.
- Import the schema in the target YugabyteDB database using the [`yb-voyager import schema`](#step-5-import-the-schema) command.
- Import the data in the target YugabyteDB database using the [`yb-voyager import data`](#step-6-import-data) command.


```goat
                                                .------------------.
                                                |    Analysis      |
                                                |                  |
                                                | .--------------. |
                                                | |Analyze schema| |
                                                | .--.-----------. |
.-------------------.     .---------------.     |    |      ^      |
|                   |     |               |     |    v      |      |
| Set up yb_voyager .---->| Export schema .---->| .---------.----. |
|                   |     |               |     | |Manual changes| |
.---------.---------.     .---------.-----.     | .--------------. |
                                                |                  |
                                                .--------.---------.
                                                         |
                                                         v
                                               .-------------.     .------------------.
                                               |             |     |      Import      |
                                               | Export data .---->|                  |
                                               |             |     | .--------------. |
                                               .-----.-------.     | |Import schema | |
                                                                   | .------.-------. |
                                                                   |        |         |
                                                                   |        v         |
                                                                   | .--------------. |
                                                                   | | Import data  | |
                                                                   | .--------------. |
                                                                   |                  |
                                                                   .--------.---------.
                                                                            |
                                                                            v
                                                                  .---------------------.
                                                                  |                     |
                                                                  | Manual verification |
                                                                  |                     |
                                                                  .---------------------.

```

## Learn more

- To learn about the steps involved in migrating to YugabyteDB, refer to [Migration phases]().

- Refer to [sources]() to learn more about the supported databases to migrate to YugabyteDB and steps to prepare your source database.

- Refer to [targets]() to learn about the YugabyteDB products you can choose from as your target database, followed by the steps to setup a target YugabyteDB database.
