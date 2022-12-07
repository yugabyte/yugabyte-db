---
title: Migration steps
linkTitle: Migration steps
description: Run the steps to ensure a successful migration.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  stable:
    identifier: migrate-steps
    parent: voyager
    weight: 102
type: docs
---

This page describes the steps to perform and verify a successful migration to YugabyteDB. Before proceeding with migration, ensure that you have completed the following steps:

- [Install yb-voyager](../install-yb-voyager/#install-yb-voyager).
- [Prepare the source database](#prepare-the-source-database).
- [Prepare the target database](#prepare-the-target-database).

## Prepare the source database

Prepare your source database by creating a new database user, and provide it with READ access to all the resources which need to be migrated.

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#postgresql" class="nav-link active" id="postgresql-tab" data-toggle="tab" role="tab" aria-controls="postgresql" aria-selected="true">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL
    </a>
  </li>
  <li>
    <a href="#mysql" class="nav-link" id="mysql-tab" data-toggle="tab" role="tab" aria-controls="mysql" aria-selected="false">
      <i class="icon-mysql" aria-hidden="true"></i>
      MySQL
    </a>
  </li>
  <li>
    <a href="#oracle" class="nav-link" id="oracle-tab" data-toggle="tab" role="tab" aria-controls="oracle" aria-selected="false">
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

If you want yb-voyager to connect to the source database over SSL, refer to [SSL Connectivity](../yb-voyager-cli/#ssl-connectivity).

{{< note title="Connecting to Oracle instances" >}}
You can use only one of the following arguments to connect to your Oracle instance.

- [`--source-db-schema`](../yb-voyager-cli/#source-db-schema)
- [`--oracle-db-sid`](../yb-voyager-cli/#oracle-db-sid)
- [`--oracle-tns-alias`](../yb-voyager-cli/##ssl-connectivity)
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

- For YugabyteDB Managed, create a user with [`yb_superuser`](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-users/#admin-and-yb-superuser) role using the following command:

     ```sql
     CREATE USER ybvoyager PASSWORD 'password';
     GRANT yb_superuser TO ybvoyager;
     ```

If you want yb-voyager to connect to the target database over SSL, refer to [SSL Connectivity](../yb-voyager-cli/#ssl-connectivity).

{{< warning title="Deleting the ybvoyager user" >}}

After migration, all the migrated objects (tables, views, and so on) are owned by the `ybvoyager` user. You should transfer the ownership of the objects to some other user (for example, `yugabyte`) and then delete the `ybvoyager` user. Example steps to delete the user are:

```sql
REASSIGN OWNED BY ybvoyager TO yugabyte;
DROP OWNED BY ybvoyager;
DROP USER ybvoyager;
```

{{< /warning >}}

## Migrate your database to YugabyteDB

Proceed with schema and data migration using the following steps:

### Export and analyze schema

To begin, export the schema from the source database. Once exported, analyze the schema and apply any necessary manual changes.

#### Export schema

<!-- To learn more about modelling strategies using YugabyteDB, refer to [Data modeling](../../yb-voyager/yb-voyager-cli/#data-modeling). -->

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
        --source-db-password <SOURCE_DB_PASSWORD> \
        --source-db-name <SOURCE_DB_NAME> \
        --source-db-schema <SOURCE_DB_SCHEMA> # Not applicable for MySQL.

```

Refer to [export schema](../yb-voyager-cli/#export-schema) for details about the arguments.

#### Analyze schema

The schema exported in the previous step may not yet be suitable for importing into YugabyteDB. Even though YugabyteDB is PostgreSQL compatible, given its distributed nature, you may need to make minor manual changes to the schema.

The `yb-voyager analyze-schema` command analyses the PostgreSQL schema dumped in the [export schema](#export-schema) step, and prepares a report that lists the DDL statements which need manual changes. An example invocation of the command is as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager analyze-schema --export-dir <EXPORT_DIR> --output-format <FORMAT>
```

The above command generates a report file under the `EXPORT_DIR/reports/` directory.

Refer to [analyze schema](../yb-voyager-cli/#analyze-schema) for details about the arguments.

#### Manually edit the schema

Fix all the issues listed in the generated schema analysis report by manually editing the SQL DDL files from the `EXPORT_DIR/schema/*`.

After making the manual changes, re-run the `yb-voyager analyze-schema` command. This generates a fresh report using your changes. Repeat these steps until the generated report contains no issues.

To learn more about modelling strategies using YugabyteDB, refer to [Data modeling](../yb-voyager-cli/#data-modeling).

{{< note title="Manual schema changes" >}}

- `CREATE INDEX CONCURRENTLY` is not currently supported in YugabyteDB. You should remove the `CONCURRENTLY` clause before trying to import the schema.

- Include the primary key definition in the `CREATE TABLE` statement. Primary Key cannot be added to a partitioned table using the `ALTER TABLE` statement.

{{< /note >}}

### Export data

Dump the source data into the `EXPORT_DIR/data` directory using the `yb-voyager export data` command as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager export data --export-dir <EXPORT_DIR> \
        --source-db-type <SOURCE_DB_TYPE> \
        --source-db-host <SOURCE_DB_HOST> \
        --source-db-user <SOURCE_DB_USER> \
        --source-db-password <SOURCE_DB_PASSWORD> \
        --source-db-name <SOURCE_DB_NAME> \
        --source-db-schema <SOURCE_DB_SCHEMA> # Not applicable for MySQL.
```

Note that the `source-db-schema` argument is required for PostgreSQL and Oracle, and is _not_ applicable for MySQL.
Refer to [export data](../yb-voyager-cli/#export-data) for details about the arguments.

The options passed to the command are similar to the [`yb-voyager export schema`](#export-schema) command. To export only a subset of the tables, pass a comma-separated list of table names in the `--table-list` argument.

### Import schema

Import the schema using the `yb-voyager import schema` command.

{{< note title="Usage for target_db_schema" >}}

The `target_db_schema` argument specifies the schema of the target database and is applicable _only for_ MySQL and Oracle.
`yb-voyager` imports the source database into the `public` schema of the target database. By specifying `--target-db-schema` argument during import, you can instruct `yb-voyager` to create a non-public schema and use it for the schema/data import.

{{< /note >}}

An example invocation of the command is as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager import schema --export-dir <EXPORT_DIR> \
        --target-db-host <TARGET_DB_HOST> \
        --target-db-user <TARGET_DB_USER> \
        --target-db-password <TARGET_DB_PASSWORD> \
        --target-db-name <TARGET_DB_NAME> \
        --target-db-schema <TARGET_DB_SCHEMA> # MySQL and Oracle only.
```

Refer to [import schema](../yb-voyager-cli/#import-schema) for details about the arguments.

yb-voyager applies the DDL SQL files located in the `$EXPORT_DIR/schema` directory to the target database. If yb-voyager terminates before it imports the entire schema, you can rerun it by adding the `--ignore-exist` option.

{{< note title="Importing indexes and triggers" >}}

Because the presence of indexes and triggers can slow down the rate at which data is imported, by default `import schema` does not import indexes and triggers. You should complete the data import without creating indexes and triggers. Only after data import is complete, you can create indexes and triggers using the `import schema` command with an additional `--post-import-data` flag.

{{< /note >}}

### Import data

After you have successfully exported the source data and imported the schema in the target database, you can import the data using the `yb-voyager import data` command as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager import data --export-dir <EXPORT_DIR> \
        --target-db-host <TARGET_DB_HOST> \
        --target-db-user <TARGET_DB_USER> \
        --target-db-password <TARGET_DB_PASSWORD> \
        --target-db-name <TARGET_DB_NAME> \
        --target-db-schema <TARGET_DB_SCHEMA> \ # MySQL and Oracle only.
        --parallel-jobs <NUMBER_OF_JOBS>
```

By default, yb-voyager creates C/2 connections where C is the total number of cores in the cluster. You can change the default number of connections using the `--parallel-jobs` argument. If yb-voyager fails to determine the number of cores in the cluster, it defaults to 2 connections per node.

Refer to [import data](../yb-voyager-cli/#import-data) for details about the arguments.

yb-voyager splits the data dump files (from the `$EXPORT_DIR/data` directory) into smaller _batches_. yb-voyager concurrently ingests the batches such that all nodes of the target YugabyteDB cluster are used. This phase is designed to be _restartable_ if yb-voyager terminates while the data import is in progress. After restarting, the data import resumes from its current state.

{{< tip title="Importing large datasets" >}}

When importing a very large database, run the import data command in a `screen` session, so that the import is not terminated when the terminal session stops.

If the `yb-voyager import data` command terminates before completing the data ingestion, you can re-run it with the same arguments and the command will resume the data import operation.

{{< /tip >}}

#### Import data status

Run the `yb-voyager import data status --export-dir <EXPORT_DIR>` command to get an overall progress of the data import operation.

#### Import data file

If all your data files are in CSV format and you have already created a schema in your target YugabyteDB, you can use the `yb-voyager import data file` command to load the data into the target table directly from the CSV file(s). This command doesn't require performing other migration steps ([export and analyze schema](#export-and-analyze-schema), [export data](#export-data), or [import schema](#import-schema) prior to import. It only requires a table present in the target database to perform the import.

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager import data file --export-dir <EXPORT_DIR> \
        --target-db-host <TARGET_DB_HOST> \
        --target-db-user <TARGET_DB_USER> \
        --target-db-password <TARGET_DB_PASSWORD> \
        --target-db-name <TARGET_DB_NAME> \
        --target-db-schema <TARGET_DB_SCHEMA> \ # MySQL and Oracle only.
        –-data-dir </path/to/files/dir/> \
        --file-table-map <filename1:table1,filename2:table2> \
        --delimiter <DELIMITER> \
        –-has-header
```

Refer to [import data file](../yb-voyager-cli/#import-data-file) for details about the arguments.

### Import indexes and triggers

Import indexes and triggers using the `import schema` command with an additional `--post-import-data` flag as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager import schema --export-dir <EXPORT_DIR> \
        --target-db-host <TARGET_DB_HOST> \
        --target-db-user <TARGET_DB_USER> \
        --target-db-password <TARGET_DB_PASSWORD> \
        --target-db-name <TARGET_DB_NAME> \
        --target-db-user <TARGET_DB_USER> \
        --target-db-schema <TARGET_DB_SCHEMA> \ # MySQL and Oracle only.
        --post-import-data
```

Refer to [import schema](../yb-voyager-cli/#import-schema) for details about the arguments.

### Verify migration

After the schema and data import is complete, the automated part of the database migration process is considered complete. You should manually run validation queries on both the source and target database to ensure that the data is correctly migrated. A sample query to validate the databases can include checking the row count of each table.

Refer to [Verify a migration](../../manage/data-migration/bulk-import-ysql/#verify-a-migration) to validate queries and ensure a successful migration.
