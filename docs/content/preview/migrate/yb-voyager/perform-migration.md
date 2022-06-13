---
title: Perform migration
linkTitle: Perform migration
description: Run the steps to ensure a successful migration.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    identifier: perform-migration
    parent: yb-voyager
    weight: 104
isTocNested: true
showAsideToc: true
---

This page describes the steps to perform and verify a successful migration to YugabyteDB. Before proceeding with migration, ensure that you have completed the following steps:

- [Install yb-voyager](../../yb-voyager/install-yb-voyager/#install-yb-voyager).
- [Prepare the source database](../../yb-voyager/install-yb-voyager/#prepare-the-source-database).
- [Prepare the target database](../../yb-voyager/install-yb-voyager/#prepare-the-target-database).

## Export and analyze schema

### Export schema

`yb-voyager export schema` command extracts the schema from the source database, converts it into PostgreSQL format (if the source database is Oracle or MySQL), and dumps the SQL DDL files in the `EXPORT_DIR/schema/*` directories.

An example invocation of the command is as follows:

```sh
yb-voyager export schema --export-dir ${EXPORT_DIR} \
        --source-db-type ${SOURCE_DB_TYPE} \
        --source-db-host ${SOURCE_DB_HOST} \
        --source-db-user ${SOURCE_DB_USER} \
        --source-db-password ${SOURCE_DB_PASSWORD} \
        --source-db-name ${SOURCE_DB_NAME} \
        --source-db-schema ${SOURCE_DB_SCHEMA}
```

{{< note title="Note" >}}
`source-db-schema` is only applicable for Oracle. Use this field only when migrating from Oracle to YugabyteDB for the [export schema](#export-schema), [analyze schema](#analyze-schema) and [export data](#export-data) phases.
{{< /note >}}

### Analyze Schema

Using [ora2pg](https://ora2pg.darold.net) and [pg_dump](https://www.postgresql.org/docs/current/app-pgdump.html), yb-voyager can extract and convert the source database schema to an equivalent PostgreSQL schema. The schema, however, may not be suitable yet to be imported into YugabyteDB. Even though YugabyteDB is PostgreSQL compatible, given its distributed nature, minor manual changes may be needed to the schema.

Refer to [Data modeling](../../yb-voyager/reference/#data-modeling) to learn more about modeling strategies using YugabyteDB.

The `yb-voyager analyze-schema` command analyses the PostgreSQL schema dumped in the [export schema](#export-schema) phase, and prepares a report that lists the DDL statements which need manual changes. An example invocation of the command is as follows:

```sh
yb-voyager analyze-schema --export-dir ${EXPORT_DIR} \
        --source-db-type ${SOURCE_DB_TYPE} \
        --source-db-host ${SOURCE_DB_HOST} \
        --source-db-user ${SOURCE_DB_USER} \
        --source-db-password ${SOURCE_DB_PASSWORD} \
        --source-db-name ${SOURCE_DB_NAME} \
        --source-db-schema ${SOURCE_DB_SCHEMA} \
        --output-format txt
```

The `--output-format` can be `html`, `txt`, `json`, or `xml`. The above command generates a report file under the `EXPORT_DIR/reports/` directory.

### Manually edit the schema

- Fix all the issues listed in the generated schema analysis report by manually editing the SQL DDL files from the `EXPORT_DIR/schema/*`.

- Re-run the `yb-voyager analyze-schema` command after making the manual changes. The command will generate a fresh report using your changes. Repeat these steps until the generated report contains no issues.

## Export data

Dump the source data into the `EXPORT_DIR/data` directory using the `yb-voyager export data` command as follows:

```sh
yb-voyager export data --export-dir ${EXPORT_DIR} \
        --source-db-type ${SOURCE_DB_TYPE} \
        --source-db-host ${SOURCE_DB_HOST} \
        --source-db-user ${SOURCE_DB_USER} \
        --source-db-password ${SOURCE_DB_PASSWORD} \
        --source-db-name ${SOURCE_DB_NAME} \
        --source-db-schema ${SOURCE_DB_SCHEMA}
```

The options passed to the command are similar to the [`yb-voyager export schema`](#export-schema) command. To export only a subset of the tables, pass a comma separated list of table names in the `--table-list` argument.

## Import schema

Import the schema with the `yb-voyager import schema` command as follows:

```sh
yb-voyager import schema --export-dir ${EXPORT_DIR} \
        --target-db-host ${TARGET_DB_HOST} \
        --target-db-port ${TARGET_DB_PORT} \
        --target-db-user ${TARGET_DB_USER} \
        --target-db-password ${TARGET_DB_PASSWORD:-''} \
        --target-db-name ${TARGET_DB_NAME}
```

yb-voyager applies the DDL SQL files located in the `$EXPORT_DIR/schema` directory to the target database. If yb-voyager terminates before it imports the entire schema, you can rerun it by adding `--ignore-exist` option.

{{< note title="Note" >}}

The `yb-voyager import schema` command does not import indexes yet. This is done to speed up the data import phase. The indexes will be created by `yb-voyager import data` command after importing the data in the next step.

{{< /note >}}

## Import data

After you have successfully exported the source data and imported the schema in the target database, you can import the data using the `yb-voyager import data` command:

```sh
yb-voyager import data --export-dir ${EXPORT_DIR} \
        --target-db-host ${TARGET_DB_HOST} \
        --target-db-port ${TARGET_DB_PORT} \
        --target-db-user ${TARGET_DB_USER} \
        --target-db-password ${TARGET_DB_PASSWORD:-''} \
        --target-db-name ${TARGET_DB_NAME}
```

The `yb-voyager import data` command reads data files located in the `EXPORT_DIR/data` directory.
yb-voyager splits the data dump files (from the `$EXPORT_DIR/data` directory) into smaller _batches_ , each of which contains at most `--batch-size` number of records. By default, the `--batch-size` is 100,000 records. yb-voyager concurrently ingests the batches such that all nodes of the target YugabyteDB cluster are utilized. This phase is designed to be _restartable_ if yb-voyager terminates when the data import is in progress. Upon a restart, the data import resumes from its current state.

By default, the command creates one database connection to each of the nodes of the target YugabyteDB cluster. You can increase the number of connections by specifying the total connection count, using the `--parallel-jobs` argument. The command will equally distribute the connections among all the nodes of the cluster.

### Import data status

Run the `yb-voyager import data status --export-dir ${EXPORT_DIR}` command to get an overall progress of the data import operation.

### Import data file

The `yb-voyager import data file` command is an alternative to the `yb-voyager import data` command, using which you can load the data into the target table directly from the file(s). This command doesn’t require performing other phases of migration :[export and analyze schema](#export-and-analyze-schema), [export data](#export-data), or [import schema](#import-schema), prior to the import data phase. It only requires a table present in the target database to perform the import.

```sh
yb-voyager import data file --export-dir ${EXPORT_DIR} \
        --target-db-host ${TARGET_DB_HOST} \
        --target-db-port ${TARGET_DB_PORT} \
        --target-db-user ${TARGET_DB_USER} \
        --target-db-password ${TARGET_DB_PASSWORD:-''} \
        --target-db-name ${TARGET_DB_NAME} \
        –-data-dir “/path/to/files/dir/” \
        --file-table-map “filename1:table1,filename2:table2” \
        --delimiter “|” \
        –-has-header \
```

{{< note title="Warnings" >}}

- While importing a very large database, you should run the import data command in a `screen` session, so that the import is not terminated when the terminal session stops.

- If the `yb-voyager import data` command terminates before it could complete the data ingestion, you can re-run it with the same arguments and the command will resume the data import operation.

{{< /note >}}

## Finalize DDL

The creation of indexes are automatically handled by the `yb-voyager import data` command after it successfully loads the data in the [import data](#import-data) phase. The command creates the indexes listed in the schema.

## Verify migration

After the successful execution of the `yb-voyager import data` command, the automated part of the database migration process is considered complete. You should manually run validation queries on both the source and target database to ensure that the data is correctly migrated. A sample query to validate the databases can include checking the row count of each table.

[Verify a migration](../../manual-import/verify-migration/) to validate queries and ensure a successful migration.
