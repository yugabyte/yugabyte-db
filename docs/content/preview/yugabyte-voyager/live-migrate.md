---
title: Steps to perform live migration of your database using YugabyteDB Voyager
headerTitle: Migration steps
linkTitle: Migrate
headcontent: Run the steps to ensure a successful migration using YugabyteDB Voyager.
description: Run the steps to ensure a successful live migration using YugabyteDB Voyager.
menu:
  preview_yugabyte-voyager:
    identifier: migrate-steps-live
    parent: yugabytedb-voyager
    weight: 103
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../migrate-steps/" class="nav-link ">
      Offline migration
    </a>
  </li>
  <li >
    <a href="../live-migrate/" class="nav-link active">
      Live migration
    </a>
  </li>
</ul>

This page describes the steps to perform and verify a successful live migration to YugabyteDB including changes that continuously occur on the source.

### Live migration workflow

The export data command will first export a snapshot and then start continuously capturing changes from the source. The import data command will similarly first import the snapshot, and then continuously apply the exported change events on the target. Eventually, the migration process will reach a steady state wherein you can perform a cutover. You can stop your applications from pointing to your source database, allow all the remaining changes to be applied on the target YugabyteDB, and then restart your applications pointing to YugabyteDB.

Before proceeding with migration, ensure that you have completed the following steps:

- [Install yb-voyager](../install-yb-voyager/#install-yb-voyager).
- Check the [unsupported features](../known-issues/#unsupported-features) and [known issues](../known-issues/#known-issues).
- Review [data modeling](../reference/data-modeling/) strategies.
- [Prepare the source database](#prepare-the-source-database).
- [Prepare the target database](#prepare-the-target-database).

## Prepare the source database

Prepare your source database by creating a new database user, and provide it with READ access to all the resources which need to be migrated.

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#oracle" class="nav-link" id="oracle-tab" data-toggle="tab" role="tab" aria-controls="oracle" aria-selected="true">
      <i class="icon-oracle" aria-hidden="true"></i>
      Oracle
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="oracle" class="tab-pane fade" role="tabpanel" aria-labelledby="oracle-tab">
  {{% includeMarkdown "./oracle.md" %}}
  </div>
</div>

If you want yb-voyager to connect to the source database over SSL, refer to [SSL Connectivity](../reference/yb-voyager-cli/#ssl-connectivity).

{{< note title="Connecting to Oracle instances" >}}
You can use only one of the following arguments to connect to your Oracle instance.

- [`--source-db-schema`](../reference/yb-voyager-cli/#source-db-schema)
- [`--oracle-db-sid`](../reference/yb-voyager-cli/#oracle-db-sid)
- [`--oracle-tns-alias`](../reference/yb-voyager-cli/#ssl-connectivity)
{{< /note >}}

## Prepare the target database

Prepare your target YugabyteDB cluster by creating a database, and a user for your cluster.

### Create the target database

Create the target database in your YugabyteDB cluster. The database name can be the same or different from the source database name. If the target database name is not provided, yb-voyager assumes the target database name to be `yugabyte`. If you do not want to import in the default `yugabyte` database, specify the name of the target database name using the `--target-db-name` argument to the `yb-voyager import` commands.

```sql
CREATE DATABASE target_db_name;
```

### Create a user

Create a user with [`SUPERUSER`](../../api/ysql/the-sql-language/statements/dcl_create_role/#syntax) role.

- For a local YugabyteDB cluster or YugabyteDB Anywhere, create a user and role with the superuser privileges using the following command:

     ```sql
     CREATE USER ybvoyager SUPERUSER PASSWORD 'password';
     ```

If you want yb-voyager to connect to the target database over SSL, refer to [SSL Connectivity](../reference/yb-voyager-cli/#ssl-connectivity).

{{< warning title="Deleting the ybvoyager user" >}}

After migration, all the migrated objects (tables, views, and so on) are owned by the `ybvoyager` user. You should transfer the ownership of the objects to some other user (for example, `yugabyte`) and then delete the `ybvoyager` user. Example steps to delete the user are:

```sql
REASSIGN OWNED BY ybvoyager TO yugabyte;
DROP OWNED BY ybvoyager;
DROP USER ybvoyager;
```

{{< /warning >}}

## Create an export directory

yb-voyager keeps all of its migration state, including exported schema and data, in a local directory called the *export directory*.

Before starting migration, you should create the export directory on a file system that has enough space to keep the entire source database. Next, you should provide the path of the export directory as a mandatory argument (`--export-dir`) to each invocation of the yb-voyager command in an environment variable.

```sh
mkdir $HOME/export-dir
export EXPORT_DIR=$HOME/export-dir
```

The export directory has the following sub-directories and files:

- `reports` directory contains the generated *Schema Analysis Report*.
- `schema` directory contains the source database schema translated to PostgreSQL. The schema is partitioned into smaller files by the schema object type such as tables, views, and so on.
- `data` directory contains TSV (Tab Separated Values) files that are passed to the COPY command on the target database.
- `metainfo` and `temp` directories are used by yb-voyager for internal bookkeeping.
- `yb-voyager.log` contains log messages.

## Migrate your database to YugabyteDB

Proceed with schema and data migration using the following steps:

### Export and analyze schema

To begin, export the schema from the source database. Once exported, analyze the schema and apply any necessary manual changes.

#### Export schema

The `yb-voyager export schema` command extracts the schema from the source database, converts it into PostgreSQL format (if the source database is Oracle or MySQL), and dumps the SQL DDL files in the `EXPORT_DIR/schema/*` directories.

{{< note title="Renaming index names for MySQL" >}}

YugabyteDB Voyager renames the indexes for MySQL migrations while exporting the schema.
MySQL supports two or more indexes to have the same name in the same database, provided they are for different tables. Similarly to PostgreSQL, YugabyteDB does not support duplicate index names in the same schema. To avoid index name conflicts during export schema, yb-voyager prefixes each index name with the associated table name.

{{< /note >}}

{{< note title="Usage for source_db_schema" >}}

The `source_db_schema` argument specifies the schema of the source database.

- For MySQL, currently the `source-db-schema` argument is not applicable.
- For PostgreSQL, `source-db-schema` can take one or more schema names separated by comma.
- For Oracle, `source-db-schema` can take only one schema name and you can migrate _only one_ schema at a time.

{{< /note >}}

An example invocation of the command is as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager export schema --export-dir <EXPORT_DIR> \
        --source-db-type <SOURCE_DB_TYPE> \
        --source-db-host <SOURCE_DB_HOST> \
        --source-db-user <SOURCE_DB_USER> \
        --source-db-password <SOURCE_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
        --source-db-name <SOURCE_DB_NAME> \
        --source-db-schema <SOURCE_DB_SCHEMA> # Not applicable for MySQL

```

Refer to [export schema](../reference/yb-voyager-cli/#export-schema) for details about the arguments.

#### Analyze schema

The schema exported in the previous step may not yet be suitable for importing into YugabyteDB. Even though YugabyteDB is PostgreSQL compatible, given its distributed nature, you may need to make minor manual changes to the schema.

The `yb-voyager analyze-schema` command analyses the PostgreSQL schema dumped in the [export schema](#export-schema) step, and prepares a report that lists the DDL statements which need manual changes. An example invocation of the command is as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager analyze-schema --export-dir <EXPORT_DIR> --output-format <FORMAT>
```

The above command generates a report file under the `EXPORT_DIR/reports/` directory.

Refer to [analyze schema](../reference/yb-voyager-cli/#analyze-schema) for details about the arguments.

#### Manually edit the schema

Fix all the issues listed in the generated schema analysis report by manually editing the SQL DDL files from the `EXPORT_DIR/schema/*`.

After making the manual changes, re-run the `yb-voyager analyze-schema` command. This generates a fresh report using your changes. Repeat these steps until the generated report contains no issues.

To learn more about modelling strategies using YugabyteDB, refer to [Data modeling](../reference/data-modeling/).

{{< note title="Manual schema changes" >}}

- `CREATE INDEX CONCURRENTLY` is not currently supported in YugabyteDB. You should remove the `CONCURRENTLY` clause before trying to import the schema.

- Include the primary key definition in the `CREATE TABLE` statement. Primary Key cannot be added to a partitioned table using the `ALTER TABLE` statement.

{{< /note >}}

### Export data

Begin exporting data from the source database into the `EXPORT_DIR/data` directory using the yb-voyager export data command as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager export data --export-dir <EXPORT_DIR> \
--source-db-type <SOURCE_DB_TYPE> \
--source-db-host <SOURCE_DB_HOST> \
--source-db-user <SOURCE_DB_USER> \
--source-db-password <SOURCE_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
--source-db-name <SOURCE_DB_NAME> \
--source-db-schema <SOURCE_DB_SCHEMA> \
--export-type snapshot-and-changes
```

After the preceding command completes successfully, export a snapshot of the data. Start capturing changes made to the data (CDC) after the snapshot. Some important metrics such as number of events, export rate, and so on will be displayed during the CDC phase.

Note that the CDC phase will start only after a snapshot of the entire interested table-set is completed.
Additionally, the CDC phase is restartable. So, if yb-voyager terminates when data export is in progress, it resumes from its current state after the CDC phase is restarted.

#### Caveats

- Some data types are unsupported. For a detailed list, refer to [datatype mappings](../reference/datatype-mapping-oracle/).
- For Oracle where sequences are not attached to a column, resume value generation is unsupported.
- [--parallel-jobs](../reference/yb-voyager-cli/#parallel-jobs) argument has no effect for live migration.

Refer to [export data](../reference/yb-voyager-cli/#export-data) for details about the arguments, and [export data status](../reference/yb-voyager-cli/#export-data-status) to track the status of an export operation.

The options passed to the command are similar to the [`yb-voyager export schema`](#export-schema) command. To export only a subset of the tables, pass a comma-separated list of table names in the `--table-list` argument.