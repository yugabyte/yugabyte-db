---
title: Reference
linkTitle: Reference
description: YugabyteDB Voyager reference information.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    identifier: reference
    parent: yb-voyager
    weight: 106
isTocNested: true
showAsideToc: true
---

## Migration workflow

A typical migration workflow using yb-voyager consists of the following steps:

- [Set up yb-voyager](../../yb-voyager/install-yb-voyager/#install-yb-voyager).
- Convert the source database schema to PostgreSQL format using the [`yb-voyager export schema`](../../yb-voyager/perform-migration/#export-schema) command.
- Generate a *Schema Analysis Report* using the [`yb-voyager analyze-schema`](../../yb-voyager/perform-migration/#analyze-schema) command. The report suggests changes to the PostgreSQL schema to make it appropriate for YugabyteDB.
- [Manually](../../yb-voyager/perform-migration/#manually-edit-the-schema) change the exported schema as suggested in the Schema Analysis Report.
- Dump the source database in the local files on the migrator machine using the [`yb-voyager export data`](../../yb-voyager/perform-migration/#export-data) command.
- Import the schema in the target YugabyteDB database using the [`yb-voyager import schema`](../../yb-voyager/perform-migration/#import-the-schema) command.
- Import the data in the target YugabyteDB database using the [`yb-voyager import data`](../../yb-voyager/perform-migration/#import-data) command.

```goat
                                              .------------------.
                                              |  Analysis        |
                                              |                  |
                                              | .--------------. |
                                              | |Analyze schema| |
                                              | .--.-----------. |
.-------------------.    .---------------.    |    |      ^      |
|                   |    |               |    |    v      |      |
| Set up yb_voyager .--->| Export schema .--->| .---------.----. |
|                   |    |               |    | |Manual changes| |
.---------.---------.    .---------.-----.    | .--------------. |
                                              |                  |
                                              .-------.----------.
                                                      |
                                                      v
                                              .-------------.    .------------------.    .------------------.
                                              |             |    |  Import          |    |                  |
                                              | Export data .--->|                  .--->| Verify migration |
                                              |             |    | .--------------. |    |                  |
                                              .-----.-------.    | |Import schema | |    .------------------.
                                                                 | .------.-------. |
                                                                 |        |         |
                                                                 |        v         |
                                                                 | .--------------. |
                                                                 | | Import data  | |
                                                                 | .--------------. |
                                                                 |                  |
                                                                 .--------.---------.
```

## Export directory

Before starting migration, you should create the export directory on a file system that has enough space to keep the entire data dump. Next, you should provide the path of the export directory as a mandatory argument (`--export-dir`) to each invocation of the yb-voyager command.

The export directory has the following sub-directories and files:

- `reports` directory contains the generated Migration Assessment Report.
- `schema` directory contains the source database schema translated to PostgreSQL. The schema is partitioned into smaller files by the schema object type such as tables, views, and so on.
- `data` directory contains TSV (Tab Separated Values) files that are passed to the COPY command on the target database.
- `metainfo` and `temp` directories are used by yb-voyager for internal bookkeeping.
- `yb-voyager.log` contains log messages.

## Data modeling

Before performing migration from your source database to YugabyteDB,

### Review your sharding strategies

YugabyteDB supports two primary ways of sharding: by HASH and RANGE. The default sharding method is set to HASH as we believe that this is the better option for most OLTP applications. You can read more about why in [Hash and range sharding](../../../architecture/docdb-sharding/sharding/). When exporting out of a PostgreSQL database, be aware that if you want RANGE partitioning, you must call it out in the schema creation.

For most workloads, it is recommended to use HASH partitioning because it efficiently partitions the data, and spreads it evenly across all nodes.

RANGE partitioning may be advantageous for particular use cases. Consider a use case which is time series specific; here you'll be querying data for specific time buckets. In this case, using RANGE partitioning to split buckets into the specific time buckets will help to improve the speed and efficiency of the query.

Additionally, you can use a combination of HASH and RANGE sharding for your primary key by choosing a HASH value as the partition key, and a RANGE value as the clustering key.

## Unsupported features

Currently, yb-voyager doesn't support the following features:

| Feature | Description/Alternatives  | Issue tracker |
| :-------| :---------- | :----------- |
| BLOB and CLOB | yb-voyager currently ignores all columns of type BLOB/CLOB. <br>  Use another mechanism to load the attributes till this feature is supported.| https://github.com/yugabyte/yb-voyager/issues/43 |
| Tablespaces |  Currently the YB migration engine cannot handle migration of tables associated with certain TABLESPACES automatically. <br> The workaround is to manually create the required tablespace in Yugabyte and then start the migration.<br> Alternatively if that tablespace is not relevant in the YugabyteDB distributed cluster then the user can remove the tablespace association of the table from the create table definition. | https://github.com/yugabyte/yb-voyager/issues/47 |
| ALTER VIEW | YugabyteDB does not yet support any schemas containing `ALTER VIEW` statements. | https://github.com/yugabyte/yb-voyager/issues/48 |
