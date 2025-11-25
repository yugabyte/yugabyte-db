---
title: Steps to perform offline migration of your database using YugabyteDB Voyager
headerTitle: Offline migration
linkTitle: Offline migration
headcontent: Steps for an offline migration using YugabyteDB Voyager
description: Run the steps to ensure a successful offline migration using YugabyteDB Voyager.
aliases:
  - /stable/yugabyte-voyager/migrate-steps/
menu:
  stable_yugabyte-voyager:
    identifier: migrate-offline
    parent: migration-types
    weight: 102
type: docs
---

The following page describes the steps to perform and verify a successful offline migration to YugabyteDB.

## Migration workflow

![Offline migration workflow](/images/migrate/migration-workflow-new.png)

| Phase | Step | Description |
| :---- | :--- | :---------- |
| PREPARE | [Install voyager](../../install-yb-voyager/#install-yb-voyager) | Voyager supports RHEL, CentOS, Ubuntu, and macOS. It can also be installed as airgapped and on Docker. |
| | [Prepare source DB](#prepare-the-source-database) | Create a new database user with READ access to all the resources to be migrated. |
| | [Prepare target DB](#prepare-the-target-database) | Deploy a YugabyteDB database and create a user with superuser privileges. |
| ASSESS | [Assess Migration](#assess-migration) | Assess the migration complexity, and get schema changes, data distribution, and cluster sizing recommendations using the `yb-voyager assess-migration` command. |
| SCHEMA | [Export schema](#export-schema) | Convert the database schema to PostgreSQL format using the `yb-voyager export schema` command. |
| |[Analyze schema](#analyze-schema) | Generate a _Schema&nbsp;Analysis&nbsp;Report_ using the `yb-voyager analyze-schema` command. The report suggests changes to the PostgreSQL schema to make it appropriate for YugabyteDB. |
| | [Modify schema](#manually-edit-the-schema) | Using the report recommendations, manually change the exported schema. |
| |[Import schema](#import-schema) | Import the modified schema to the target YugabyteDB database using the `yb-voyager import schema` command. |
| DATA | [Export data](#export-data) | Dump the source database to the target machine (where yb-voyager is installed), using the `yb-voyager export data` command. |
| | [Import data](#import-data) | Import the data to the target YugabyteDB database using the `yb-voyager import data` command. |
| | [Finalize schema post data import](#finalize-schema-post-data-import) | Restore NOT VALID constraints and refresh materialized views (if any) in the target YugabyteDB database using the `yb-voyager finalize-schema-post-data-import` command.|
| | [Verify](#verify-migration) | Check if the offline migration is successful. |
| END | [End migration](#end-migration) | Clean up the migration information stored in the export directory and databases (source and target). |

Before proceeding with migration, ensure that you have completed the following steps:

- [Install yb-voyager](../../install-yb-voyager/#install-yb-voyager).
- Review the [guidelines for your migration](../../known-issues/).
- Review [data modeling](../../../develop/data-modeling/) strategies.
- [Prepare the source database](#prepare-the-source-database).
- [Prepare the target database](#prepare-the-target-database).

## Prepare the source database

Create a new database user, and assign the necessary user permissions.

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#postgresql" class="nav-link active" id="postgresql-tab" data-bs-toggle="tab" role="tab" aria-controls="postgresql" aria-selected="true">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL
    </a>
  </li>
  <li>
    <a href="#mysql" class="nav-link" id="mysql-tab" data-bs-toggle="tab" role="tab" aria-controls="mysql" aria-selected="true">
      <i class="icon-mysql" aria-hidden="true"></i>
      MySQL
    </a>
  </li>
  <li>
    <a href="#oracle" class="nav-link" id="oracle-tab" data-bs-toggle="tab" role="tab" aria-controls="oracle" aria-selected="true">
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

If you intend to perform a migration assessment, note that the assessment provides recommendations on which tables in the source database to colocate. To ensure that you will be able to colocate tables, create your target database with colocation set to TRUE using the following command:

```sql
CREATE DATABASE target_db_name WITH COLOCATION = true;
```

### Create a user

Create a user with [`SUPERUSER`](../../../api/ysql/the-sql-language/statements/dcl_create_role/#syntax) role.

- For a local YugabyteDB cluster or YugabyteDB Anywhere, create a user and role with the superuser privileges using the following command:

     ```sql
     CREATE USER ybvoyager SUPERUSER PASSWORD 'password';
     ```

- For YugabyteDB Aeon, create a user with [`yb_superuser`](../../../yugabyte-cloud/cloud-secure-clusters/cloud-users/#admin-and-yb-superuser) role using the following command:

     ```sql
     CREATE USER ybvoyager PASSWORD 'password';
     GRANT yb_superuser TO ybvoyager;
     ```

If you want yb-voyager to connect to the target YugabyteDB database over SSL, refer to [SSL Connectivity](../../reference/yb-voyager-cli/#ssl-connectivity).

Alternatively, if you want to proceed with migration without a superuser, refer to [Import data without a superuser](../../reference/non-superuser/).

## Create an export directory

yb-voyager keeps all of its migration state, including exported schema and data, in a local directory called the _export directory_.

Before starting migration, you should create the export directory on a file system that has enough space to keep the entire source database. Ideally, create this export directory inside a parent folder named after your migration for better organization. You need to provide the full path to the export directory in the `export-dir` parameter of your [configuration file](#set-up-a-configuration-file), or in the `--export-dir` flag when running `yb-voyager` commands.

```sh
mkdir -p $HOME/<migration-name>/export-dir
```

The export directory has the following sub-directories and files:

- `reports` directory contains the generated _Schema Analysis Report_.
- `schema` directory contains the source database schema translated to PostgreSQL. The schema is partitioned into smaller files by the schema object type such as tables, views, and so on.
- `data` directory contains CSV (Comma Separated Values) files that are passed to the COPY command on the target YugabyteDB database.
- `metainfo` and `temp` directories are used by yb-voyager for internal bookkeeping.
- `logs` directory contains the log files for each command.

## Set up a configuration file

You can use a [configuration file](../../reference/configuration-file/) to specify the parameters required when running Voyager commands (v2025.6.2 or later).

To get started, copy the `offline-migration.yaml` template configuration file from one of the following locations to the migration folder you created (for example, `$HOME/my-migration/`):

{{< tabpane text=true >}}

  {{% tab header="Linux (apt/yum/airgapped)" lang="linux" %}}

```bash
/opt/yb-voyager/config-templates/offline-migration.yaml
```

  {{% /tab %}}

  {{% tab header="MacOS (Homebrew)" lang="macos" %}}

```bash
$(brew --cellar)/yb-voyager@<voyager-version>/<voyager-version>/config-templates/offline-migration.yaml
```

Replace `<voyager-version>` with your installed Voyager version, for example, `2025.5.2`.

  {{% /tab %}}

{{< /tabpane >}}

Set the export-dir, source, and target arguments in the configuration file:

```yaml
# Replace the argument values with those applicable for your migration.

export-dir: <absolute-path-to-export-dir>

source:
  db-type: <source-db-type>
  db-host: <source-db-host>
  db-port: <source-db-port>
  db-name: <source-db-name>
  db-schema: <source-db-schema> # Not applicable for MySQL
  db-user: <source-db-user>
  db-password: <source-db-password> # Enclose the password in single quotes if it contains special characters.

target:
  db-host: <target-db-host>
  db-port: <target-db-port>
  db-name: <target-db-name>
  db-schema: <target-db-schema> # MySQL and Oracle only
  db-user: <target-db-username>
  db-password: <target-db-password> # Enclose the password in single quotes if it contains special characters.
```

Refer to the [offline-migration.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/offline-migration.yaml) template for more information on the available global, source, and target configuration parameters supported by Voyager.

### Configure yugabyted UI

You can use [yugabyted UI](/stable/reference/configuration/yugabyted/) to view the migration assessment report, and to visualize and review the database migration workflow performed by YugabyteDB Voyager.

Configure the yugabyted UI as follows:

  1. Start a local YugabyteDB cluster. Refer to the steps described in [Use a local cluster](/stable/quick-start/macos/). Skip this step if you already have a local YugabyteDB cluster as your [target database](#prepare-the-target-database).

  1. Set the Control Plane configuration parameters:

        ```yaml
        ### *********** Control Plane Configuration ************
        ### To see the Voyager migration workflow details in the UI, set the following parameters.

        ### Control plane type refers to the deployment type of YugabyteDB
        ### Accepted values: yugabyted
        ### Optional (if not set, no visualization will be available)
        control-plane-type: yugabyted

        ### Yugabyted Control Plane Configuration (for local yugabyted clusters)
        ### Uncomment the section below if control-plane-type is 'yugabyted'
        yugabyted-control-plane:
          ### YSQL connection string to yugabyted database
          ### Provide standard PostgreSQL connection parameters: user name, host name, and port
          ### Example: postgresql://yugabyte:yugabyte@127.0.0.1:5433
          ### Note: Don't include the dbname parameter; the default 'yugabyte' database is used for metadata
          db-conn-string: postgresql://yugabyte:yugabyte@127.0.0.1:5433
        ```

## Assess migration

This step applies to PostgreSQL and Oracle migrations only.

Assess migration analyzes the source database, captures essential metadata, and generates a report with recommended migration strategies and cluster configurations for optimal performance with YugabyteDB.

You run assessments using the `yb-voyager assess-migration` command as follows:

1. Choose from one of the supported modes for conducting migration assessments, depending on your access to the source database as follows:<br><br>

    {{< tabpane text=true >}}

    {{% tab header="With source database connectivity" %}}

This mode requires direct connectivity to the source database from the client machine where voyager is installed. You initiate the assessment by executing the `assess-migration` command of `yb-voyager`. This command facilitates a live analysis by interacting directly with the source database, to gather metadata required for assessment. A sample command is as follows:

```sh
yb-voyager assess-migration --source-db-type postgresql \
    --source-db-host hostname --source-db-user ybvoyager \
    --source-db-password password --source-db-name dbname \
    --source-db-schema schema1,schema2 --export-dir /path/to/export/dir
```

If you are using a [configuration file](../../reference/configuration-file/), use the following:

```sh
yb-voyager assess-migration --config-file <path-to-config-file>
```

  {{% /tab %}}

  {{% tab header="Without source database connectivity" %}}

PostgreSQL only. In situations where direct access to the source database is restricted, there is an alternative approach. Voyager includes packages with scripts for PostgreSQL at `/etc/yb-voyager/gather-assessment-metadata`.

You can perform the following steps with these scripts:

1. On a machine which has access to the source database, copy the scripts and install dependencies psql and pg_dump version 14 or later. Alternatively, you can install yb-voyager on the machine to automatically get the dependencies.

1. Run the `yb-voyager-pg-gather-assessment-metadata.sh` script by providing the source connection string, the schema names, path to a directory where metadata will be saved, and an optional argument of an interval to capture the IOPS metadata of the source (in seconds with a default value of 120). For example:

    ```sh
    /path/to/yb-voyager-pg-gather-assessment-metadata.sh 'postgresql://ybvoyager@host:port/dbname' 'schema1|schema2' '/path/to/assessment_metadata_dir' '60'
    ```

1. Copy the metadata directory to the client machine on which voyager is installed, and run the `assess-migration` command by specifying the path to the metadata directory as follows:

    ```sh
    yb-voyager assess-migration --source-db-type postgresql \
        --assessment-metadata-dir /path/to/assessment_metadata_dir --export-dir /path/to/export/dir
    ```

    If you are using a [configuration file](../../reference/configuration-file/), use the following:

    ```sh
    yb-voyager assess-migration --config-file <path-to-config-file>
    ```

    {{% /tab %}}

    {{< /tabpane >}}

1. The output is a migration assessment report, and its path is printed on the console. To view the assessment report, navigate to the **Migrations** tab in the [yugabyted UI](#configure-yugabyted-ui) at <http://127.0.0.1:15433> to see the available migrations.

    {{< warning title="Important" >}}
For the most accurate migration assessment, the source database must be actively handling its typical workloads at the time the metadata is gathered. This ensures that the recommendations for sharding strategies and cluster sizing are well-aligned with the database's real-world performance and operational needs.
    {{< /warning >}}

1. Resize your target YugabyteDB cluster in [Enhanced PostgreSQL Compatibility Mode](../../../reference/configuration/postgresql-compatibility/), based on the sizing recommendations in the assessment report.

   If you are using YugabyteDB Anywhere, [enable compatibility mode](../../../reference/configuration/postgresql-compatibility/#yugabytedb-anywhere) by setting the **More > Edit Postgres Compatibility** option.

1. If the assessment recommended creating some tables as colocated, check that your target YugabyteDB database is colocated in [ysqlsh](/stable/api/ysqlsh/) using the following command:

    ```sql
    select yb_is_database_colocated();
    ```

Refer to [Migration assessment](../../migrate/assess-migration/) for more information.

## Migrate your database to YugabyteDB

Proceed with schema and data migration using the following steps:

### Export and analyze schema

To begin, export the schema from the source database. Once exported, analyze the schema and apply any necessary manual changes.

#### Export schema

The `yb-voyager export schema` command extracts the schema from the source database, converts it into PostgreSQL format (if the source database is Oracle or MySQL), and dumps the SQL DDL files in the `EXPORT_DIR/schema/*` directories.

**For PostgreSQL migrations**:

- Recommended schema optimizations from the [assess migration](#assess-migration) report are applied to ensure YugabyteDB compatibility and optimal performance.
- A **Schema Optimization Report**, with details and an explanation of every change, is generated for your review.

**For MySQL migrations**:

- YugabyteDB Voyager renames the indexes to avoid naming conflicts. MySQL allows two or more indexes to have the same name in the same database, provided they are for different tables. Like PostgreSQL, YugabyteDB does not support duplicate index names in the same schema. To avoid index name conflicts during export schema, Voyager prefixes each index name with the associated table name.

You specify the schema(s) to migrate from the source database using the `db-schema` parameter (configuration file), or `--source-db-schema` flag (CLI).

- For MySQL, currently this argument is not applicable.
- For PostgreSQL, this argument can take one or more schema names separated by comma.
- For Oracle, this argument can take only one schema name and you can migrate _only one_ schema at a time.

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

```sh
yb-voyager export schema --config-file <path-to-config-file>
```

You can specify additional `export schema` parameters in the `export-schema` section of the configuration file. For more details, refer to the [offline-migration.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/offline-migration.yaml) template.

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

```bash
# Replace the argument values with those applicable for your migration.
yb-voyager export schema --export-dir <EXPORT_DIR> \
        --source-db-type <SOURCE_DB_TYPE> \
        --source-db-host <SOURCE_DB_HOST> \
        --source-db-user <SOURCE_DB_USER> \
        --source-db-password <SOURCE_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
        --source-db-name <SOURCE_DB_NAME> \
        --source-db-schema <SOURCE_DB_SCHEMA> # Not applicable for MySQL
```

  {{% /tab %}}

{{< /tabpane >}}

Refer to [export schema](../../reference/schema-migration/export-schema/) for more information on the use of the command.

#### Analyze schema

The schema exported in the previous step may not yet be suitable for importing into YugabyteDB. Even though YugabyteDB is PostgreSQL compatible, given its distributed nature, you may need to make minor manual changes to the schema.

The `yb-voyager analyze-schema` command analyses the PostgreSQL schema dumped in the [export schema](#export-schema) step, and prepares a report that lists the DDL statements which need manual changes.

By default, the report is output in HTML and JSON format. You can optionally change the format for the output (options are `html`, `txt`, `json`, or `xml`) using the `output-format` parameter (configuration file) or flag (CLI).

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

```sh
yb-voyager analyze-schema --config-file <path-to-config-file>
```

You can specify additional `analyze-schema` parameters in the `analyze-schema` section of the configuration file. For more details, refer to the [offline-migration.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/offline-migration.yaml) template.

 {{% /tab %}}

{{% tab header="CLI" lang="cli" %}}

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager analyze-schema --export-dir <EXPORT_DIR> --output-format <FORMAT>
```

  {{% /tab %}}

{{< /tabpane >}}

Refer to [analyze schema](../../reference/schema-migration/analyze-schema/) for more information on the use of the command.

#### Manually edit the schema

Fix all the issues listed in the generated schema analysis report by manually editing the SQL DDL files from the `EXPORT_DIR/schema/*`.

After making the manual changes, re-run the `yb-voyager analyze-schema` command. This generates a fresh report using your changes. Repeat these steps until the generated report contains no issues.

To learn more about modelling strategies using YugabyteDB, refer to [Data modeling](../../../develop/data-modeling/).

{{< note title="Manual schema changes" >}}

Include the primary key definition in the `CREATE TABLE` statement. Primary Key cannot be added to a partitioned table using the `ALTER TABLE` statement.

{{< /note >}}

Refer to the [Manual review guideline](../../known-issues/) for a detailed list of limitations and suggested workarounds associated with the source databases when migrating to YugabyteDB Voyager.

### Import schema

Import the schema using the `yb-voyager import schema` command.

The `db-schema` key inside the `target` section parameters (configuration file), or the `--target-db-schema` flag (CLI), is used to specify the schema in the target YugabyteDB database where the source schema will be imported and is applicable _only for_ MySQL and Oracle. `yb-voyager` imports the source database into the `public` schema of the target YugabyteDB database. By specifying this argument during import, you can instruct `yb-voyager` to create a non-public schema and use it for the schema/data import.

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

```sh
yb-voyager import schema --config-file <path-to-config-file>
```

You can specify additional `import schema` parameters in the `import-schema` section of the configuration file. For more details, refer to the [offline-migration.yaml](<https://github.com/yugabyte/yb-voyager/blob/{{>< yb-voyager-release >}}/yb-voyager/config-templates/offline-migration.yaml) template.

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager import schema --export-dir <EXPORT_DIR> \
        --target-db-host <TARGET_DB_HOST> \
        --target-db-user <TARGET_DB_USER> \
        --target-db-password <TARGET_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name <TARGET_DB_NAME> \
        --target-db-schema <TARGET_DB_SCHEMA> # MySQL and Oracle only
```

  {{% /tab %}}

{{< /tabpane >}}

Refer to [import schema](../../reference/schema-migration/import-schema/) for more information on the use of the command.

{{< note title="NOT VALID constraints are not imported" >}}

Currently, `import schema` does not import NOT VALID constraints exported from source, because this could lead to constraint violation errors during the import if the source contains the data that is violating the constraint.

Voyager will add these constraints back during [Finalize schema post data import](#finalize-schema-post-data-import).

{{< /note >}}

yb-voyager applies the DDL SQL files located in the `schema` sub-directory of the [export directory](#create-an-export-directory) to the target YugabyteDB database. If `yb-voyager` terminates before it imports the entire schema, you can rerun it by adding the `ignore-exist` argument (configuration file), or using the `--ignore-exist` flag (CLI).

### Export data

You can export the source data using the `yb-voyager export data` command. This command dumps the source database data into CSV files in the `EXPORT_DIR/data` directory.

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

```sh
yb-voyager export data --config-file <path-to-config-file>
```

You can specify additional `export data` parameters in the `export-data` section of the configuration file. For more details, refer to the [offline-migration.yaml](<https://github.com/yugabyte/yb-voyager/blob/{{>< yb-voyager-release >}}/yb-voyager/config-templates/offline-migration.yaml) template.

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

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

  {{% /tab %}}

{{< /tabpane >}}

{{< note title="PostgreSQL and parallel jobs" >}}
For PostgreSQL, make sure that no other processes are running on the source database that can try to take locks; with more than one parallel job, Voyager will not be able to take locks to dump the data.
{{< /note >}}

Note that the `db-schema` argument (configuration file) or the `--source-db-schema` flag (CLI) is required for PostgreSQL and Oracle, and is _not_ applicable for MySQL.

The options passed to the command are similar to the [`yb-voyager export schema`](#export-schema) command. To export only a subset of tables, provide a comma-separated list of table names using the `table-list` argument (configuration file), or pass it via the `--table-list` flag (CLI).

Refer to [export data](../../reference/data-migration/export-data) for more information.

{{< note title="Sequence migration considerations" >}}

Sequence migration consists of two steps: sequence creation and setting resume value (resume value refers to the `NEXTVAL` of a sequence on a source database). A sequence object is generated during export schema and the resume values for sequences are generated during export data. These resume values are then set on the target YugabyteDB database just after the data is imported for all tables.

Note that there are some special cases involving sequences such as the following:

- In MySQL, auto-increment column is migrated to YugabyteDB as a normal column with a sequence attached to it.
- For PostgreSQL, only the sequences attached to or owned by the migrating tables are restored during the migration.

{{< /note >}}

{{< note title= "Migrating source databases with large row sizes" >}}
If a table's row size on the source database is too large, and exceeds the default [RPC message size](../../../reference/configuration/all-flags-yb-master/#rpc-max-message-size), import data will fail with the error `ERROR: Sending too long RPC message..`. So, you need to migrate those tables separately after removing the large rows.
{{< /note >}}

#### Export data status

To get an overall progress of the export data operation, you can run the `yb-voyager export data status` command. You specify the `<EXPORT_DIR>` to push data in using `export-dir` parameter (configuration file), or `--export-dir` flag (CLI).

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

```sh
yb-voyager export data status --config-file <path-to-config-file>
```

You can specify additional `export data status` parameters in the `export-data-status` section of the configuration file. For more details, refer to the [offline-migration.yaml](<https://github.com/yugabyte/yb-voyager/blob/{{>< yb-voyager-release >}}/yb-voyager/config-templates/offline-migration.yaml) template.

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

```sh
yb-voyager export data status --export-dir <EXPORT_DIR>` command.
```

  {{% /tab %}}

{{< /tabpane >}}

Refer to [export data status](../../reference/data-migration/export-data/#export-data-status) for more information.

#### Accelerate data export for MySQL and Oracle

For MySQL and Oracle, you can optionally speed up data export by setting the argument `beta-fast-data-export: 1` in the `export-data` section of the configuration file when you run `export data`. Alternatively, you can set the environment variable `BETA_FAST_DATA_EXPORT=1`.

Consider the following caveats before using the feature:

- You need to perform additional steps when you [prepare the source database](#prepare-the-source-database).
- Some data types are unsupported. For a detailed list, refer to [datatype mappings](../../reference/datatype-mapping-mysql/).
- The `parallel-jobs` argument (configuration file) or `--parallel-jobs` flag (CLI), which specifies the number of tables to be exported in parallel from the source database, will have no effect.
- In MySQL RDS, writes are not allowed during the data export process.
- Sequences that are not associated with any column or attached to columns of non-integer types are not supported for resuming value generation.

### Import data

After you have successfully exported the source data and imported the schema in the target YugabyteDB database, you can import the data using the `yb-voyager import data` command with required arguments.

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

```sh
yb-voyager import data --config-file <path-to-config-file>
```

You can specify additional `import data` parameters in the `import-data` section of the configuration file. For more details, refer to the [offline-migration.yaml](<https://github.com/yugabyte/yb-voyager/blob/{{>< yb-voyager-release >}}/yb-voyager/config-templates/offline-migration.yaml) template.

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager import data --export-dir <EXPORT_DIR> \
        --target-db-host <TARGET_DB_HOST> \
        --target-db-user <TARGET_DB_USER> \
        --target-db-password <TARGET_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name <TARGET_DB_NAME> \
        --target-db-schema <TARGET_DB_SCHEMA> \ # MySQL and Oracle only.
```

  {{% /tab %}}

{{< /tabpane >}}

By default, yb-voyager imports data in parallel using multiple connections, and adapts the parallelism based on the resource usage of the cluster. Refer to [Techniques to improve performance](../../reference/performance/#techniques-to-improve-performance) for more details on tuning performance.

Refer to [import data](../../reference/data-migration/import-data/) for more information.

yb-voyager splits the data dump files (from the `$EXPORT_DIR/data` directory) into smaller _batches_. yb-voyager concurrently ingests the batches such that all nodes of the target YugabyteDB database cluster are used. This phase is designed to be _restartable_ if yb-voyager terminates while the data import is in progress. After restarting, the data import resumes from its current state.

{{< tip title="Importing large datasets" >}}

When importing a very large database, run the import data command in a `screen` session, so that the import is not terminated when the terminal session stops.

If the `yb-voyager import data` command terminates before completing the data ingestion, you can re-run it with the same arguments and the command will resume the data import operation.

{{< /tip >}}

{{< note title= "Migrating source databases with large row sizes" >}}
When exporting data using the `beta-fast-data-export` configuration parameter or the `BETA_FAST_DATA_EXPORT` environment variable, the import data process has a default row size limit of 32MB. If a row exceeds this limit but is smaller than the `batch-size * max-row-size`, you can increase the limit for the import data process by setting the following configuration parameter in import data to handle such rows:

```yaml
import-data:
  csv-reader-max-buffer-size-bytes: <MAX_ROW_SIZE_IN_BYTES>
```

Alternatively, you can export the following environment variable:

```sh
export CSV_READER_MAX_BUFFER_SIZE_BYTES = <MAX_ROW_SIZE_IN_BYTES>
```

{{< /note >}}

#### Import data status

To get an overall progress of the import data operation, you can run the `yb-voyager import data status` command. You specify the `<EXPORT_DIR>` to push data in using `export-dir` parameter (configuration file), or `--export-dir` flag (CLI).

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

```sh
yb-voyager import data status --config-file <path-to-config-file>
```

You can specify additional `import data status` parameters in the `import-data-status` section of the configuration file. For more details, refer to the [offline-migration.yaml](<https://github.com/yugabyte/yb-voyager/blob/{{>< yb-voyager-release >}}/yb-voyager/config-templates/offline-migration.yaml) template.

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

```sh
yb-voyager import data status --export-dir <EXPORT_DIR>` command.
```

  {{% /tab %}}

{{< /tabpane >}}

Refer to [import data status](../../reference/data-migration/import-data/#import-data-status) for more information.

### Finalize schema post data import

If there are any NOT VALID constraints on the source, create them after the import data command is completed by using the `finalize-schema-post-data-import` command. If there are [Materialized views](../../../explore/ysql-language-features/advanced-features/views/#materialized-views) in the target YugabyteDB database, you can refresh them by setting the `refresh-mviews` parameter in the `finalize-schema-post-data-import` (configuration file) or use `--refresh-mviews` flag (CLI) with the value true.
Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

```sh
yb-voyager finalize-schema-post-data-import --config-file <path-to-config-file>
```

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager finalize-schema-post-data-import --export-dir <EXPORT_DIR> \
       --target-db-host <TARGET_DB_HOST> \
       --target-db-user <TARGET_DB_USER> \
       --target-db-password <TARGET_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
       --target-db-name <TARGET_DB_NAME> \
       --target-db-schema <TARGET_DB_SCHEMA> \ # MySQL and Oracle only
```

  {{% /tab %}}

{{< /tabpane >}}

Refer to [finalize-schema-post-data-import](../../reference/schema-migration/finalize-schema-post-data-import/) for more information.

{{< note title ="Note" >}}
The `import schema --post-snapshot-import` command is deprecated. Use [finalize-schema-post-data-import](../../reference/schema-migration/finalize-schema-post-data-import/) instead.
{{< /note >}}

### Verify migration

After the schema and data import is complete, manually run validation queries on both the source and target YugabyteDB database to ensure that the data is correctly migrated. For example, you can validate the databases by running queries to check the row count of each table.

{{< warning title = "Caveat associated with rows reported by import data status" >}}

Suppose you have the following scenario:

- The [import data](#import-data) or [import data file](../bulk-data-load/#import-data-files-from-the-local-disk) command fails.
- To resolve this issue, you delete some of the rows from the split files.
- After retrying, the import data command completes successfully.

In this scenario, the [import data status](#import-data-status) command reports an incorrect imported row count, because it doesn't take into account the deleted rows.

For more details, refer to GitHub issue [#360](https://github.com/yugabyte/yb-voyager/issues/360).

{{< /warning >}}

### Validate query performance

{{<tags/feature/tp>}} You can compare query performance between the source database and the target YugabyteDB database using the [yb-voyager compare-performance](../../reference/compare-performance/) command.

This command analyzes statistics collected during [assess migration](../assess-migration/) from the source database and compares it with statistics collected from the target YugabyteDB database.

The command generates both HTML and JSON reports and provides insight into how your workload (application queries) is performing on the target YugabyteDB database compared to the source database, allowing you to prioritize and plan optimization efforts.

#### Prerequisites

To compare query performance, verify you have done the following:

- Performed a [migration assessment](../../migrate/assess-migration/) using the [assess-migration](../../reference/assess-migration/#assess-migration) command, and have the statistics from the source database.
- Run a source workload on both the source and target YugabyteDB databases.
- Enabled statistics collection ([pg_stat_statements](../../../additional-features/pg-extensions/extension-pgstatstatements/)) on the target YugabyteDB database.

#### Compare performance

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

```sh
yb-voyager compare-performance --config-file <path-to-config-file>
```

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager compare-performance --export-dir <EXPORT_DIR> \
       --target-db-host <TARGET_DB_HOST> \
       --target-db-user <TARGET_DB_USER> \
       --target-db-password <TARGET_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
       --target-db-name <TARGET_DB_NAME> \
       --target-db-schema <TARGET_DB_SCHEMA> \ # MySQL and Oracle only
```

  {{% /tab %}}

{{< /tabpane >}}

Refer to [yb-voyager compare-performance](../../reference/compare-performance/) for more information.

## End migration

To complete the migration, you need to clean up the export directory (export-dir) and Voyager state (Voyager-related metadata) stored in the target YugabyteDB database.

The `yb-voyager end migration` command performs the cleanup, and backs up the schema, data, migration reports, and log files by providing the backup related arguments.

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

Specify the following parameters in the `end-migration` section of the configuration file:

```yaml
...
end-migration:
  backup-schema-files: <true, false, yes, no, 1, 0>
  backup-data-files: <true, false, yes, no, 1, 0>
  save-migration-reports: <true, false, yes, no, 1, 0>
  backup-log-files: <true, false, yes, no, 1, 0>
  # Set optional argument to store a back up of any of the above  arguments.
  backup-dir: <BACKUP_DIR>
...
```

Run the command:

```sh
yb-voyager end migration --config-file <path-to-config-file>
```

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

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

  {{% /tab %}}

{{< /tabpane >}}

After you run end migration, you will _not_ be able to continue further.

If you want to back up the schema, data, log files, and the migration reports (`analyze-schema` report, `export data status` output, or `import data status` output) for future reference, use the `backup-dir` argument (configuration file) or `--backup-dir` flag (CLI), and provide the path of the directory where you want to save the backup content (based on what you choose to back up).

Refer to [end migration](../../reference/end-migration/) for more information.

### Delete the ybvoyager user (Optional)

After migration, all the migrated objects (tables, views, and so on) are owned by the `ybvoyager` user. Transfer the ownership of the objects to some other user (for example, `yugabyte`) and then delete the `ybvoyager` user. For example, do the following:

```sql
REASSIGN OWNED BY ybvoyager TO yugabyte;
DROP OWNED BY ybvoyager;
DROP USER ybvoyager;
```
