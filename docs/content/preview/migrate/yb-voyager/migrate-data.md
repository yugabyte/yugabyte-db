---
title: Migrate your data
linkTitle: Migrate your data
description: Run the steps to ensure a successful migration.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    identifier: migrate-data
    parent: yb-voyager
    weight: 104
isTocNested: true
showAsideToc: true
---

This page describes the steps to perform and verify a successful migration to YugabyteDB. Before proceeding with migration, ensure that you have completed the following steps:

- [Install yb-voyager](../../yb-voyager/prerequisites/#install-yb-voyager).
- [Prepare the source database](../../yb-voyager/prepare-databases/#prepare-the-source-database).
- [Prepare the target database](../../yb-voyager/prepare-databases/#prepare-the-target-database).

## Export and analyze schema

To begin, export the schema from the source database. Once exported, analyze the schema and apply any necessary manual changes.

### Export schema

Using [ora2pg](https://ora2pg.darold.net) and [pg_dump](https://www.postgresql.org/docs/current/app-pgdump.html), yb-voyager can extract and convert the source database schema to an equivalent PostgreSQL schema.

To learn more about modelling strategies using YugabyteDB, refer to [Data modeling](../../yb-voyager/reference/#data-modeling).

The `yb-voyager export schema` command extracts the schema from the source database, converts it into PostgreSQL format (if the source database is Oracle or MySQL), and dumps the SQL DDL files in the `EXPORT_DIR/schema/*` directories.

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
The `source-db-schema` field is only used for Oracle migrations. Use this field only when migrating from Oracle for the [export schema](#export-schema), [analyze schema](#analyze-schema), and [export data](#export-data) phases.
{{< /note >}}

### Analyze schema

The schema exported in the previous step may not yet be suitable for importing into YugabyteDB. Even though YugabyteDB is PostgreSQL compatible, given its distributed nature, you may need to make minor manual changes to the schema.

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

Fix all the issues listed in the generated schema analysis report by manually editing the SQL DDL files from the `EXPORT_DIR/schema/*`.

After making the manual changes, re-run the `yb-voyager analyze-schema` command. This generates a fresh report using your changes. Repeat these steps until the generated report contains no issues.

{{< note title="Manual schema changes" >}}

- `CREATE INDEX CONCURRENTLY` is not currently supported in YugabyteDB. You should remove the `CONCURRENTLY` clause before trying to import the schema.

- Include the primary key definition in the `CREATE TABLE` statement. Primary Key cannot be added to a partitioned table using the `ALTER TABLE` statement.

{{< /note >}}

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

The options passed to the command are similar to the [`yb-voyager export schema`](#export-schema) command. To export only a subset of the tables, pass a comma-separated list of table names in the `--table-list` argument.

## Import schema

Import the schema using the `yb-voyager import schema` command as follows:

```sh
yb-voyager import schema --export-dir ${EXPORT_DIR} \
        --target-db-host ${TARGET_DB_HOST} \
        --target-db-user ${TARGET_DB_USER} \
        --target-db-password ${TARGET_DB_PASSWORD:-''} \
        --target-db-name ${TARGET_DB_NAME}
```

yb-voyager applies the DDL SQL files located in the `$EXPORT_DIR/schema` directory to the target database. If yb-voyager terminates before it imports the entire schema, you can rerun it by adding the `--ignore-exist` option.

{{< note title="Note" >}}

To speed up data import, the `yb-voyager import schema` command doesn't import indexes. The indexes are created by the `yb-voyager import data` command in the next step.

{{< /note >}}

## Import data

After you have successfully exported the source data and imported the schema in the target database, you can import the data using the `yb-voyager import data` command:

```sh
yb-voyager import data --export-dir ${EXPORT_DIR} \
        --target-db-host ${TARGET_DB_HOST} \
        --target-db-user ${TARGET_DB_USER} \
        --target-db-password ${TARGET_DB_PASSWORD:-''} \
        --target-db-name ${TARGET_DB_NAME}
```

yb-voyager splits the data dump files (from the `$EXPORT_DIR/data` directory) into smaller _batches_ , each of which contains at most `--batch-size` number of records. By default, the `--batch-size` is 100,000 records. yb-voyager concurrently ingests the batches such that all nodes of the target YugabyteDB cluster are used. This phase is designed to be _restartable_ if yb-voyager terminates while the data import is in progress. After restarting, the data import resumes from its current state.

By default, the `yb-voyager import data` command creates one database connection to each of the nodes of the target YugabyteDB cluster. You can increase the number of connections by specifying the total connection count, using the `--parallel-jobs` argument with the `import data` command. The command distributes the connections equally to all the nodes of the cluster.

### Import data status

Run the `yb-voyager import data status --export-dir ${EXPORT_DIR}` command to get an overall progress of the data import operation.

### Import data file

Use the `yb-voyager import data file` command if you have all your data files in csv format, and if you have already created a schema in your target YugabyteDB. This command allows you to load the data into the target table directly from the csv file(s). This command doesn’t require performing other phases of migration :[export and analyze schema](#export-and-analyze-schema), [export data](#export-data), or [import schema](#import-schema), prior to the import data phase. It only requires a table present in the target database to perform the import.

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

{{< tip title="Importing large datasets" >}}

When importing a very large database, run the import data command in a `screen` session, so that the import is not terminated when the terminal session stops.

If the `yb-voyager import data` command terminates before completing the data ingestion, you can re-run it with the same arguments and the command will resume the data import operation.

{{< /tip >}}

## Finalize DDL

The `yb-voyager import data` command automatically creates indexes after it successfully loads the data in the [import data](#import-data) phase. The command creates the indexes listed in the schema.

## Verify migration

After the `yb-voyager import data` command completes, the automated part of the database migration process is considered complete. You should manually run validation queries on both the source and target database to ensure that the data is correctly migrated. A sample query to validate the databases can include checking the row count of each table.

Refer to [Verify a migration](../../manual-import/verify-migration/) to validate queries and ensure a successful migration.
