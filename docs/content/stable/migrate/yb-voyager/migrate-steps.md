---
title: Migration steps
linkTitle: Migration steps
description: Run the steps to ensure a successful migration.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  stable:
    identifier: migrate-steps
    parent: yb-voyager
    weight: 102
type: docs
---

This page describes the steps to perform and verify a successful migration to YugabyteDB. Before proceeding with migration, ensure that you have completed the following steps:

- [Install yb-voyager](../../yb-voyager/install-yb-voyager/#install-yb-voyager).
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

{{< note title="Note" >}}

- For PostgreSQL, yb-voyager supports migrating _all_ schemas of the source database. It does not support migrating _only a subset_ of the schemas.

- For Oracle, you can migrate only one schema at a time.

{{< /note >}}

If you want yb-voyager to connect to the source database over SSL, refer to [SSL Connectivity](/preview/migrate/yb-voyager/yb-voyager-cli/#ssl-connectivity).

## Prepare the target database

Prepare your target YugabyteDB cluster by creating a database, and a user for your cluster.

### Create the target database

1. Create the target database in your YugabyteDB cluster. The database name can be the same or different from the source database name. If the target database name is not provided, yb-voyager assumes the target database name to be `yugabyte`. If you do not want to import in the default `yugabyte` database, specify the name of the target database name using the `--target-db-name` argument to the `yb-voyager import` commands.

   ```sql
   CREATE DATABASE target_db_name;
   ```

1. Capture the database name in an environment variable.

   ```sh
   export TARGET_DB_NAME=target_db_name
   ```

### Create a user

1. Create a user with [`SUPERUSER`](../../../api/ysql/the-sql-language/statements/dcl_create_role/#syntax) role.

   - For a local YugabyteDB cluster or YugabyteDB Anywhere versions below 2.13.1 or 2.12.4, create a user and role with the superuser privileges using the following command:

     ```sql
     CREATE USER ybvoyager SUPERUSER PASSWORD 'password';
     ```

   - For YugabyteDB Managed or YugabyteDB Anywhere versions (2.13.1 and above) or (2.12.4 and above), create a user with [`yb_superuser`](../../../yugabyte-cloud/cloud-secure-clusters/cloud-users/#admin-and-yb-superuser) role using the following command:

     ```sql
     CREATE USER ybvoyager PASSWORD 'password';
     GRANT yb_superuser TO ybvoyager;
     ```

1. Capture the user and database details in environment variables.

   ```sh
   export TARGET_DB_HOST=127.0.0.1
   export TARGET_DB_PORT=5433
   export TARGET_DB_USER=ybvoyager
   export TARGET_DB_PASSWORD=password
   ```

If you want yb-voyager to connect to the target database over SSL, refer to [SSL Connectivity](/preview/migrate/yb-voyager/yb-voyager-cli/#ssl-connectivity).

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

Using [ora2pg](https://ora2pg.darold.net) and [pg_dump](https://www.postgresql.org/docs/current/app-pgdump.html), yb-voyager can extract and convert the source database schema to an equivalent PostgreSQL schema.

<!-- To learn more about modelling strategies using YugabyteDB, refer to [Data modeling](../../yb-voyager/yb-voyager-cli/#data-modeling). -->

The `yb-voyager export schema` command extracts the schema from the source database, converts it into PostgreSQL format (if the source database is Oracle or MySQL), and dumps the SQL DDL files in the `EXPORT_DIR/schema/*` directories.

An example invocation of the command is as follows:

```sh
yb-voyager export schema --export-dir ${EXPORT_DIR} \
        --source-db-type ${SOURCE_DB_TYPE} \
        --source-db-host ${SOURCE_DB_HOST} \
        --source-db-user ${SOURCE_DB_USER} \
        --source-db-password ${SOURCE_DB_PASSWORD} \
        --source-db-name ${SOURCE_DB_NAME}
```

<!-- ```sh
yb-voyager export schema --export-dir /path/to/yb/export/dir
        --source-db-type postgresql #or mysql
        --source-db-host localhost
        --source-db-password password
        --source-db-name dbname
        --source-db-user username
``` -->

{{< note title="Note" >}}
The `source-db-schema` argument is only used for Oracle migrations. Use this argument only when migrating from Oracle for the [export schema](#export-schema), [analyze schema](#analyze-schema), and [export data](#export-data) steps.
{{< /note >}}

An example invocation of the command for Oracle is as follows:

```sh
yb-voyager export schema --export-dir ${EXPORT_DIR} \
        --source-db-type ${SOURCE_DB_TYPE} \
        --source-db-host ${SOURCE_DB_HOST} \
        --source-db-user ${SOURCE_DB_USER} \
        --source-db-password ${SOURCE_DB_PASSWORD} \
        --source-db-schema ${SOURCE_DB_SCHEMA}
```

#### Analyze schema

The schema exported in the previous step may not yet be suitable for importing into YugabyteDB. Even though YugabyteDB is PostgreSQL compatible, given its distributed nature, you may need to make minor manual changes to the schema.

The `yb-voyager analyze-schema` command analyses the PostgreSQL schema dumped in the [export schema](#export-schema) step, and prepares a report that lists the DDL statements which need manual changes. An example invocation of the command is as follows:

```sh
yb-voyager analyze-schema --export-dir ${EXPORT_DIR} \
        --source-db-type ${SOURCE_DB_TYPE} \
        --source-db-host ${SOURCE_DB_HOST} \
        --source-db-user ${SOURCE_DB_USER} \
        --source-db-password ${SOURCE_DB_PASSWORD} \
        --source-db-name ${SOURCE_DB_NAME} \
        --output-format txt
```

<!-- ```sh
yb-voyager analyze-schema --export-dir /path/to/yb/export/dir \
        --source-db-type postgresql \ # or mysql
        --source-db-host localhost \
        --source-db-user username \
        --source-db-password password \
        --source-db-name dbname \
        --output-format txt
``` -->

The `--output-format` can be `html`, `txt`, `json`, or `xml`. The above command generates a report file under the `EXPORT_DIR/reports/` directory.

An example invocation of the command for Oracle is as follows:

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

#### Manually edit the schema

Fix all the issues listed in the generated schema analysis report by manually editing the SQL DDL files from the `EXPORT_DIR/schema/*`.

After making the manual changes, re-run the `yb-voyager analyze-schema` command. This generates a fresh report using your changes. Repeat these steps until the generated report contains no issues.

To learn more about modelling strategies using YugabyteDB, refer to [Data modeling](../../yb-voyager/yb-voyager-cli/#data-modeling).

{{< note title="Manual schema changes" >}}

- `CREATE INDEX CONCURRENTLY` is not currently supported in YugabyteDB. You should remove the `CONCURRENTLY` clause before trying to import the schema.

- Include the primary key definition in the `CREATE TABLE` statement. Primary Key cannot be added to a partitioned table using the `ALTER TABLE` statement.

{{< /note >}}

### Export data

Dump the source data into the `EXPORT_DIR/data` directory using the `yb-voyager export data` command as follows:

```sh
yb-voyager export data --export-dir ${EXPORT_DIR} \
        --source-db-type ${SOURCE_DB_TYPE} \
        --source-db-host ${SOURCE_DB_HOST} \
        --source-db-user ${SOURCE_DB_USER} \
        --source-db-password ${SOURCE_DB_PASSWORD} \
        --source-db-name ${SOURCE_DB_NAME} \
```

<!-- ```sh
yb-voyager export data --export-dir /path/to/yb/export/dir \
        --source-db-type postgresql \ #or mysql
        --source-db-host localhost \
        --source-db-user username \
        --source-db-password password \
        --source-db-name dbname
``` -->

An example invocation of the command for Oracle is as follows:

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

### Import schema

Import the schema using the `yb-voyager import schema` command as follows:

```sh
yb-voyager import schema --export-dir ${EXPORT_DIR} \
        --target-db-host ${TARGET_DB_HOST} \
        --target-db-user ${TARGET_DB_USER} \
        --target-db-password ${TARGET_DB_PASSWORD} \
        --target-db-name ${TARGET_DB_NAME}
```

<!-- ```sh
yb-voyager import schema --export-dir /path/to/yb/export/dir \
        --target-db-host localhost \
        --target-db-password password \
        --target-db-name dbname \
        --target-db-user username
``` -->

For Oracle, `yb-voyager` imports the source database into the `public` schema of the target database. By specifying `--target-db-schema` argument during import, you can instruct `yb-voyager` to create a non-public schema and use it for the schema/data import.

```sh
yb-voyager import schema --export-dir ${EXPORT_DIR} \
        --target-db-host ${TARGET_DB_HOST} \
        --target-db-user ${TARGET_DB_USER} \
        --target-db-password ${TARGET_DB_PASSWORD} \
        --target-db-name ${TARGET_DB_NAME} \
        --target-db-user ${TARGET_DB_USER}
```

yb-voyager applies the DDL SQL files located in the `$EXPORT_DIR/schema` directory to the target database. If yb-voyager terminates before it imports the entire schema, you can rerun it by adding the `--ignore-exist` option.

{{< note title="Note" >}}

To speed up data import, the `yb-voyager import schema` command doesn't import indexes. The indexes are created by the `yb-voyager import data` command in the next step.

{{< /note >}}

### Import data

After you have successfully exported the source data and imported the schema in the target database, you can import the data using the `yb-voyager import data` command:

```sh
yb-voyager import data --export-dir ${EXPORT_DIR} \
        --target-db-host ${TARGET_DB_HOST} \
        --target-db-user ${TARGET_DB_USER} \
        --target-db-password ${TARGET_DB_PASSWORD} \
        --target-db-name ${TARGET_DB_NAME} \
        --target-db-schema ${TARGET_DB_SCHEMA}
```

<!-- ```sh
yb-voyager import data --export-dir /path/to/yb/export/dir \
        --target-db-host localhost \
        --target-db-user username \
        --target-db-password password \
        --target-db-name dbname \
        --target-db-schema schemaName
``` -->

yb-voyager splits the data dump files (from the `$EXPORT_DIR/data` directory) into smaller _batches_ , each of which contains at most `--batch-size` number of records. By default, the `--batch-size` is 100,000 records. yb-voyager concurrently ingests the batches such that all nodes of the target YugabyteDB cluster are used. This phase is designed to be _restartable_ if yb-voyager terminates while the data import is in progress. After restarting, the data import resumes from its current state.

By default, the `yb-voyager import data` command creates one database connection to each of the nodes of the target YugabyteDB cluster. You can increase the number of connections by specifying the total connection count, using the `--parallel-jobs` argument with the `import data` command. The command distributes the connections equally to all the nodes of the cluster.

```sh
yb-voyager import data --export-dir ${EXPORT_DIR} \
        --target-db-host ${TARGET_DB_HOST} \
        --target-db-user ${TARGET_DB_USER} \
        --target-db-password ${TARGET_DB_PASSWORD} \
        --target-db-name ${TARGET_DB_NAME} \
        --target-db-schema ${TARGET_DB_SCHEMA} \
        --parallel-jobs 100 \
        --batch-size 250000
```

{{< tip title="Importing large datasets" >}}

When importing a very large database, run the import data command in a `screen` session, so that the import is not terminated when the terminal session stops.

If the `yb-voyager import data` command terminates before completing the data ingestion, you can re-run it with the same arguments and the command will resume the data import operation.

{{< /tip >}}

#### Import data status

Run the `yb-voyager import data status --export-dir ${EXPORT_DIR}` command to get an overall progress of the data import operation.

#### Import data file

If all your data files are in csv format and you have already created a schema in your target YugabyteDB, you can use the `yb-voyager import data file` command to load the data into the target table directly from the csv file(s). This command doesn’t require performing other migration steps ([export and analyze schema](#export-and-analyze-schema), [export data](#export-data), or [import schema](#import-schema) prior to import. It only requires a table present in the target database to perform the import.

<!-- ```sh
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
``` -->

```sh
yb-voyager import data file --export-dir ${EXPORT_DIR} \
        --target-db-host ${TARGET_DB_HOST} \
        --target-db-user ${TARGET_DB_USER} \
        --target-db-password ${TARGET_DB_PASSWORD} \
        --target-db-name ${TARGET_DB_NAME} \
        –-data-dir "/path/to/files/dir/" \
        --file-table-map "filename1:table1,filename2:table2" \
        --delimiter "|" \
        –-has-header
```

### Finalize DDL

The `yb-voyager import data` command automatically creates indexes after it successfully loads the data in the [import data](#import-data) phase. The command creates the indexes listed in the schema.

### Verify migration

After the `yb-voyager import data` command completes, the automated part of the database migration process is considered complete. You should manually run validation queries on both the source and target database to ensure that the data is correctly migrated. A sample query to validate the databases can include checking the row count of each table.

Refer to [Verify a migration](../../manual-import/verify-migration/) to validate queries and ensure a successful migration.
