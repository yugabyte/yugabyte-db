---
title: Steps to perform offline migration of your database using YugabyteDB Voyager
headerTitle: Offline migration
linkTitle: Offline migration
headcontent: Steps for an offline migration using YugabyteDB Voyager
description: Run the steps to ensure a successful offline migration using YugabyteDB Voyager.
aliases:
  - /preview/yugabyte-voyager/migrate-steps/
menu:
  preview_yugabyte-voyager:
    identifier: migrate-offline
    parent: migration-types
    weight: 102
type: docs
---

The following page describes the steps to perform and verify a successful offline migration to YugabyteDB.

## Migration workflow

![Offline migration workflow](/images/migrate/migration-workflow-new.png)

| Step | Description |
| :--- | :---|
| [Install yb-voyager](../../install-yb-voyager/#install-yb-voyager) | yb-voyager supports RHEL, CentOS, Ubuntu, and macOS, as well as airgapped and Docker-based installations. |
| [Prepare source](#prepare-the-source-database) | Create a new database user with READ access to all the resources to be migrated. |
| [Prepare target](#prepare-the-target-database) | Deploy a YugabyteDB database and create a user with superuser privileges. |
| [Export schema](#export-schema) | Convert the database schema to PostgreSQL format using the `yb-voyager export schema` command. |
| [Analyze schema](#analyze-schema) | Generate a *Schema&nbsp;Analysis&nbsp;Report* using the `yb-voyager analyze-schema` command. The report suggests changes to the PostgreSQL schema to make it appropriate for YugabyteDB. |
| [Modify schema](#manually-edit-the-schema) | Using the report recommendations, manually change the exported schema. |
| [Export data](#export-data) | Dump the source database to the target machine (where yb-voyager is installed), using the `yb-voyager export data` command. |
| [Import schema](#import-schema) | Import the modified schema to the target YugabyteDB database using the `yb-voyager import schema` command. |
| [Import data](#import-data) | Import the data to the target YugabyteDB database using the `yb-voyager import data` command. |
| [Import&nbsp;indexes&nbsp;and triggers](#import-indexes-and-triggers) | Import indexes and triggers to the target YugabyteDB database using the `yb-voyager import schema` command with an additional `--post-import-data` flag. |
| [Verify migration](#verify-migration) | Check if the offline migration is successful. |
| [End migration](#end-migration) | Clean up the migration information stored in the export directory and databases (source and target). |

Before proceeding with migration, ensure that you have completed the following steps:

- [Install yb-voyager](../../install-yb-voyager/#install-yb-voyager).
- Review the [guidelines for your migration](../../known-issues/).
- Review [data modeling](../../reference/data-modeling/) strategies.
- [Prepare the source database](#prepare-the-source-database).
- [Prepare the target database](#prepare-the-target-database).

## Prepare the source database

Create a new database user, and assign the necessary user permissions.

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#postgresql" class="nav-link active" id="postgresql-tab" data-toggle="tab" role="tab" aria-controls="postgresql" aria-selected="true">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL
    </a>
  </li>
  <li>
    <a href="#mysql" class="nav-link" id="mysql-tab" data-toggle="tab" role="tab" aria-controls="mysql" aria-selected="true">
      <i class="icon-mysql" aria-hidden="true"></i>
      MySQL
    </a>
  </li>
  <li>
    <a href="#oracle" class="nav-link" id="oracle-tab" data-toggle="tab" role="tab" aria-controls="oracle" aria-selected="true">
      <i class="icon-oracle" aria-hidden="true"></i>
      Oracle
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="postgresql" class="tab-pane fade show active" role="tabpanel" aria-labelledby="postgresql-tab">
  {{% includeMarkdown "./postgresql.md" %}}
  </div>
  <div id="mysql" class="tab-pane fade" role="tabpanel" aria-labelledby="mysql-tab">
  {{% includeMarkdown "./mysql.md" %}}
  </div>
  <div id="oracle" class="tab-pane fade" role="tabpanel" aria-labelledby="oracle-tab">
  {{% includeMarkdown "./oracle.md" %}}
  </div>
</div>

## Prepare the target database

Prepare your target YugabyteDB database cluster by creating a database, and a user for your cluster.

### Create the target database

Create the target YugabyteDB database in your YugabyteDB cluster. The database name can be the same or different from the source database name. If the target YugabyteDB database name is not provided, yb-voyager assumes the target YugabyteDB database name to be `yugabyte`. If you do not want to import to the default `yugabyte` database, specify the name of the target YugabyteDB database using the `--target-db-name` argument of the `yb-voyager import` command.

```sql
CREATE DATABASE target_db_name;
```

### Create a user

Create a user with [`SUPERUSER`](../../../api/ysql/the-sql-language/statements/dcl_create_role/#syntax) role.

- For a local YugabyteDB cluster or YugabyteDB Anywhere, create a user and role with the superuser privileges using the following command:

     ```sql
     CREATE USER ybvoyager SUPERUSER PASSWORD 'password';
     ```

- For YugabyteDB Managed, create a user with [`yb_superuser`](../../../yugabyte-cloud/cloud-secure-clusters/cloud-users/#admin-and-yb-superuser) role using the following command:

     ```sql
     CREATE USER ybvoyager PASSWORD 'password';
     GRANT yb_superuser TO ybvoyager;
     ```

If you want yb-voyager to connect to the target YugabyteDB database over SSL, refer to [SSL Connectivity](../../reference/yb-voyager-cli/#ssl-connectivity).

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
- `data` directory contains CSV (Comma Separated Values) files that are passed to the COPY command on the target YugabyteDB database.
- `metainfo` and `temp` directories are used by yb-voyager for internal bookkeeping.
- `logs` directory contains the log files for each command.

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

An example invocation of the command with required arguments is as follows:

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

Refer to [export schema](../../reference/schema-migration/export-schema/) for details about the arguments.

#### Analyze schema

The schema exported in the previous step may not yet be suitable for importing into YugabyteDB. Even though YugabyteDB is PostgreSQL compatible, given its distributed nature, you may need to make minor manual changes to the schema.

The `yb-voyager analyze-schema` command analyses the PostgreSQL schema dumped in the [export schema](#export-schema) step, and prepares a report that lists the DDL statements which need manual changes. An example invocation of the command An example invocation of the command with required arguments is as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager analyze-schema --export-dir <EXPORT_DIR> --output-format <FORMAT>
```

The above command generates a report file under the `EXPORT_DIR/reports/` directory.

Refer to [analyze schema](../../reference/schema-migration/analyze-schema/) for details about the arguments.

#### Manually edit the schema

Fix all the issues listed in the generated schema analysis report by manually editing the SQL DDL files from the `EXPORT_DIR/schema/*`.

After making the manual changes, re-run the `yb-voyager analyze-schema` command. This generates a fresh report using your changes. Repeat these steps until the generated report contains no issues.

To learn more about modelling strategies using YugabyteDB, refer to [Data modeling](../../reference/data-modeling/).

{{< note title="Manual schema changes" >}}

Include the primary key definition in the `CREATE TABLE` statement. Primary Key cannot be added to a partitioned table using the `ALTER TABLE` statement.

{{< /note >}}

Refer to the [Manual review guideline](../../known-issues/) for a detailed list of limitations and suggested workarounds associated with the source databases when migrating to YugabyteDB Voyager.

### Export data

Dump the source data into the `EXPORT_DIR/data` directory using the `yb-voyager export data` command as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager export data --export-dir <EXPORT_DIR> \
        --source-db-type <SOURCE_DB_TYPE> \
        --source-db-host <SOURCE_DB_HOST> \
        --source-db-user <SOURCE_DB_USER> \
        --source-db-password <SOURCE_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
        --source-db-name <SOURCE_DB_NAME> \
        --source-db-schema <SOURCE_DB_SCHEMA> # Not applicable for MySQL
```

Note that the `source-db-schema` argument is required for PostgreSQL and Oracle, and is _not_ applicable for MySQL.
Refer to [export data](../../reference/data-migration/export-data) for details about the arguments.

The options passed to the command are similar to the [`yb-voyager export schema`](#export-schema) command. To export only a subset of the tables, pass a comma-separated list of table names in the `--table-list` argument.

{{< note title="Sequence migration considerations" >}}

Sequence migration consists of two steps: sequence creation and setting resume value (resume value refers to the `NEXTVAL` of a sequence on a source database). A sequence object is generated during export schema and the resume values for sequences are generated during export data. These resume values are then set on the target YugabyteDB database just after the data is imported for all tables.

Note that there are some special cases involving sequences such as the following:

- In MySQL, auto-increment column is migrated to YugbayteDB as a normal column with a sequence attached to it.
- For PostgreSQL, `SERIAL` datatype and `GENERATED AS IDENTITY` columns use sequence object internally, so resume values for them are also generated during data export.

{{< /note >}}

#### Export data status

Run the `yb-voyager export data status --export-dir <EXPORT_DIR>` command to get an overall progress of the export data operation.

Refer to [export data status](../../reference/data-migration/export-data/#export-data-status) for details about the arguments.

#### Accelerate data export for MySQL and Oracle

For MySQL and Oracle, you can optionally speed up data export by setting the environment variable `BETA_FAST_DATA_EXPORT=1` when you run `export data` using yb-voyager.

Consider the following caveats before using the feature:

- You need to perform additional steps when you [prepare the source database](#prepare-the-source-database).
- Some data types are unsupported. For a detailed list, refer to [datatype mappings](../../reference/datatype-mapping-mysql/).
- `--parallel-jobs` argument (specifies the number of tables to be exported in parallel from the source database at a time) will have no effect.
- In MySQL RDS, writes are not allowed during the data export process.
- For Oracle where sequences are not attached to a column, resume value generation is unsupported.

### Import schema

Import the schema using the `yb-voyager import schema` command.

{{< note title="Usage for target_db_schema" >}}

The `target_db_schema` argument specifies the schema of the target YugabyteDB database and is applicable _only for_ MySQL and Oracle.
`yb-voyager` imports the source database into the `public` schema of the target YugabyteDB database. By specifying `--target-db-schema` argument during import, you can instruct `yb-voyager` to create a non-public schema and use it for the schema/data import.

{{< /note >}}

An example invocation of the command with required arguments is as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager import schema --export-dir <EXPORT_DIR> \
        --target-db-host <TARGET_DB_HOST> \
        --target-db-user <TARGET_DB_USER> \
        --target-db-password <TARGET_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name <TARGET_DB_NAME> \
        --target-db-schema <TARGET_DB_SCHEMA> # MySQL and Oracle only
```

Refer to [import schema](../../reference/schema-migration/import-schema/) for details about the arguments.

yb-voyager applies the DDL SQL files located in the `$EXPORT_DIR/schema` directory to the target YugabyteDB database. If yb-voyager terminates before it imports the entire schema, you can rerun it by adding the `--ignore-exist` option.

{{< note title="Importing indexes and triggers" >}}

Because the presence of indexes and triggers can slow down the rate at which data is imported, by default `import schema` does not import indexes and triggers (with the exception of UNIQUE indexes, to avoid any issues during import of schema because of foreign key dependencies on the index). You should complete the data import without creating indexes and triggers. After data import is complete, create indexes and triggers using the `import schema` command with an additional `--post-import-data` flag.

{{< /note >}}

### Import data

After you have successfully exported the source data and imported the schema in the target YugabyteDB database, you can import the data using the `yb-voyager import data` command with required arguments as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager import data --export-dir <EXPORT_DIR> \
        --target-db-host <TARGET_DB_HOST> \
        --target-db-user <TARGET_DB_USER> \
        --target-db-password <TARGET_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name <TARGET_DB_NAME> \
        --target-db-schema <TARGET_DB_SCHEMA> \ # MySQL and Oracle only.
        --parallel-jobs <NUMBER_OF_JOBS>
```

By default, yb-voyager creates C/2 connections where C is the total number of cores in the cluster. You can change the default number of connections using the `--parallel-jobs` argument. If yb-voyager fails to determine the number of cores in the cluster, it defaults to 2 connections per node.

Refer to [import data](../../reference/data-migration/import-data/) for details about the arguments.

yb-voyager splits the data dump files (from the `$EXPORT_DIR/data` directory) into smaller _batches_. yb-voyager concurrently ingests the batches such that all nodes of the target YugabyteDB database cluster are used. This phase is designed to be _restartable_ if yb-voyager terminates while the data import is in progress. After restarting, the data import resumes from its current state.

{{< tip title="Importing large datasets" >}}

When importing a very large database, run the import data command in a `screen` session, so that the import is not terminated when the terminal session stops.

If the `yb-voyager import data` command terminates before completing the data ingestion, you can re-run it with the same arguments and the command will resume the data import operation.

{{< /tip >}}

#### Import data status

Run the `yb-voyager import data status --export-dir <EXPORT_DIR>` command to get an overall progress of the data import operation.

Refer to [import data status](../../reference/data-migration/import-data/#import-data-status) for details about the arguments.

### Import indexes and triggers

Import indexes and triggers using the `import schema` command with an additional `--post-import-data` flag as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager import schema --export-dir <EXPORT_DIR> \
        --target-db-host <TARGET_DB_HOST> \
        --target-db-user <TARGET_DB_USER> \
        --target-db-password <TARGET_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name <TARGET_DB_NAME> \
        --target-db-schema <TARGET_DB_SCHEMA> \ # MySQL and Oracle only
        --post-import-data true
```

Refer to [import schema](../../reference/schema-migration/import-schema/) for details about the arguments.

### Verify migration

After the schema and data import is complete, manually run validation queries on both the source and target YugabyteDB database to ensure that the data is correctly migrated. For example, you can validate the databases by running queries to check the row count of each table.

{{< warning title = "Caveat associated with rows reported by import data status" >}}

Suppose you have a scenario where,

- [import data](#import-data) or [import data file](../bulk-data-load/#import-data-files-from-the-local-disk) command fails.
- To resolve this issue, you delete some of the rows from the split files.
- After retrying, the import data command completes successfully.

In this scenario, [import data status](#import-data-status) command reports incorrect imported row count; because it doesn't take into account the deleted rows.

For more details, refer to the GitHub issue [#360](https://github.com/yugabyte/yb-voyager/issues/360).

{{< /warning >}}

### End migration

To complete the migration, you need to clean up the export directory (export-dir) and Voyager state (Voyager-related metadata) stored in the target YugabyteDB database.

Run the `yb-voyager end migration` command to perform the clean up, and to back up the schema, data, migration reports, and log files by providing the backup related flags (mandatory) as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager end migration --export-dir <EXPORT_DIR> \
        --backup-log-files <true, false, yes, no, 1, 0> \
        --backup-data-files <true, false, yes, no, 1, 0> \
        --backup-schema-files <true, false, yes, no, 1, 0> \
        --save-migration-reports <true, false, yes, no, 1, 0> \
        # Set optional argument to store a back up of any of the above arguments.
        --backup-dir <BACKUP_DIR>
```

After you run end migration, you will _not_ be able to continue further.

If you want to back up the schema, data, log files, and the migration reports (`analyze-schema` report, `export data status` output, or `import data status` output) for future reference, use the `--backup-dir` argument, and provide the path of the directory where you want to save the backup content (based on what you choose to back up).

Refer to [end migration](../../reference/end-migration/) for more details on the arguments.
