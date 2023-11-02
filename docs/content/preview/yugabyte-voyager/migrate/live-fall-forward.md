---
title: Steps to perform live migration with fall-forward using YugabyteDB Voyager
headerTitle: Live migration with fall-forward
linkTitle: Live migration with fall-forward
headcontent: Steps for a live migration with fall-forward using YugabyteDB Voyager
description: Steps to ensure a successful live migration with fall-forward using YugabyteDB Voyager.
menu:
  preview_yugabyte-voyager:
    identifier: live-fall-forward
    parent: migration-types
    weight: 103
techPreview: /preview/releases/versioning/#feature-availability
rightNav:
  hideH4: true
type: docs
---

When migrating using YugabyteDB Voyager, it is prudent to have a backup strategy if the new database doesn't work as expected. A fall-forward approach consists of creating a third database (the fall-forward database) that is a replica of your original source database.

A fall forward approach allows you to test the system end-to-end. This workflow is especially important in heterogeneous migration scenarios, in which source and target databases are using different engines.

## Fall-forward workflow

![fall-forward short](/images/migrate/live-fall-forward-short.png)

Before starting a [live migration](../live-migrate/#live-migration-workflow), you set up the fall-forward database (via [fall-forward setup](#fall-forward-setup)). During migration, yb-voyager replicates the snapshot data along with new changes exported from the source database to the target and fall-forward databases, as shown in the following illustration:

![After fall-forward setup](/images/migrate/after-fall-forward-setup.png)

At [cutover](#cutover-to-the-target), applications stop writing to the source database and start writing to the target YugabyteDB database. After the cutover process is complete, YB Voyager keeps the fall-forward database synchronized with changes from the target Yugabyte DB as shown in the following illustration:

![After cutover](/images/migrate/after-cutover.png)

Finally, if you need to switch to the fall-forward database (because the current YugabyteDB system is not working as expected), you can [switch over your database](#switch-over-to-the-fall-forward-optional).

![After fall-forward switchover](/images/migrate/after-fall-fwd-switchover.png)

The following illustration describes the workflow for live migration using YB Voyager with the fall-forward option.

![Live migration with fall-forward workflow](/images/migrate/live-fall-forward.png)

Before proceeding with migration, ensure that you have completed the following steps:

- [Install yb-voyager](../../install-yb-voyager/#install-yb-voyager).
- Check the [unsupported features](../../known-issues/#unsupported-features) and [known issues](../../known-issues/#known-issues).
- Review [data modeling](../../reference/data-modeling/) strategies.
- [Prepare the source database](#prepare-the-source-database).
- [Prepare the target database](#prepare-the-target-database).

## Prepare the source database

Create a new database user, and assign the necessary user permissions.

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#standalone-oracle" class="nav-link active" id="standalone-oracle-tab" data-toggle="tab" role="tab" aria-controls="oracle" aria-selected="true">
      <i class="icon-oracle" aria-hidden="true"></i>
      Standalone Oracle Container Database
    </a>
  </li>
    <li>
    <a href="#rds-oracle" class="nav-link" id="rds-oracle-tab" data-toggle="tab" role="tab" aria-controls="oracle" aria-selected="true">
      <i class="icon-oracle" aria-hidden="true"></i>
      RDS Oracle
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="standalone-oracle" class="tab-pane fade show active" role="tabpanel" aria-labelledby="standalone-oracle-tab">
  {{% includeMarkdown "./standalone-oracle.md" %}}
  </div>
    <div id="rds-oracle" class="tab-pane fade" role="tabpanel" aria-labelledby="rds-oracle-tab">
  {{% includeMarkdown "./rds-oracle.md" %}}
  </div>
</div>

If you want yb-voyager to connect to the source database over SSL, refer to [SSL Connectivity](../../reference/yb-voyager-cli/#ssl-connectivity).

{{< note title="Connecting to Oracle instances" >}}
You can use only one of the following arguments to connect to your Oracle instance.

- --source-db-schema (Schema name of the source database.)
- --oracle-db-sid (Oracle System Identifier you can use while exporting data from Oracle instances.)
- --oracle-tns-alias (TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server.)
{{< /note >}}

## Prepare the target database

Prepare your target YugabyteDB database cluster by creating a database, and a user for your cluster.

### Create the target database

Create the target database in your YugabyteDB cluster. The database name can be the same or different from the source database name. If the target database name is not provided, yb-voyager assumes the target database name to be `yugabyte`. If you do not want to import in the default `yugabyte` database, specify the name of the target database name using the `--target-db-name` argument to the `yb-voyager import` commands.

```sql
CREATE DATABASE target_db_name;
```

### Create a user

Create a user with [`SUPERUSER`](../../../api/ysql/the-sql-language/statements/dcl_create_role/#syntax) role.

- For a local YugabyteDB cluster or YugabyteDB Anywhere, create a user and role with the superuser privileges using the following command:

     ```sql
     CREATE USER ybvoyager SUPERUSER PASSWORD 'password';
     ```

If you want yb-voyager to connect to the target database over SSL, refer to [SSL Connectivity](../../reference/yb-voyager-cli/#ssl-connectivity).

{{< warning title="Deleting the ybvoyager user" >}}

After migration, all the migrated objects (tables, views, and so on) are owned by the `ybvoyager` user. You should transfer the ownership of the objects to some other user (for example, `yugabyte`) and then delete the `ybvoyager` user. Example steps to delete the user are:

```sql
REASSIGN OWNED BY ybvoyager TO yugabyte;
DROP OWNED BY ybvoyager;
DROP USER ybvoyager;
```

{{< /warning >}}

## Create an export directory

yb-voyager keeps all of its migration state, including exported schema and data, in a local directory called the _export directory_.

Before starting migration, you should create the export directory on a file system that has enough space to keep the entire source database. Next, you should provide the path of the export directory as a mandatory argument (`--export-dir`) to each invocation of the yb-voyager command in an environment variable.

```sh
mkdir $HOME/export-dir
export EXPORT_DIR=$HOME/export-dir
```

The export directory has the following sub-directories and files:

- `reports` directory contains the generated _Schema Analysis Report_.
- `schema` directory contains the source database schema translated to PostgreSQL. The schema is partitioned into smaller files by the schema object type such as tables, views, and so on.
- `data` directory contains CSV (Comma Separated Values) files that are passed to the COPY command on the target database.
- `metainfo` and `temp` directories are used by yb-voyager for internal bookkeeping.
- `logs` directory contains the log files for each command.

## Prepare fall-forward database

Perform the following steps to prepare your fall-forward database:

1. Create `ybvoyager_metadata` schema or user, and tables, and assign its privileges to `ybvoyager` as follows:

    ```sql
    CREATE USER ybvoyager_metadata IDENTIFIED BY "password";
    GRANT CONNECT, RESOURCE TO ybvoyager_metadata;
    ALTER USER ybvoyager_metadata QUOTA UNLIMITED ON USERS;

    CREATE TABLE ybvoyager_metadata.ybvoyager_import_data_batches_metainfo_v2 (
                data_file_name VARCHAR2(250),
                batch_number NUMBER(10),
                schema_name VARCHAR2(250),
                table_name VARCHAR2(250),
                rows_imported NUMBER(19),
                PRIMARY KEY (data_file_name, batch_number, schema_name, table_name)
            );

    CREATE TABLE ybvoyager_metadata.ybvoyager_import_data_event_channels_metainfo (
                migration_uuid VARCHAR2(36),
                channel_no INT,
                last_applied_vsn NUMBER(19),
                num_inserts NUMBER(19),
                num_updates NUMBER(19),
                num_deletes NUMBER(19),
                PRIMARY KEY (migration_uuid, channel_no)
            );

    CREATE TABLE ybvoyager_metadata.ybvoyager_imported_event_count_by_table (
            migration_uuid VARCHAR2(36),
            table_name VARCHAR2(250),
            channel_no INT,
            total_events NUMBER(19),
            num_inserts NUMBER(19),
            num_updates NUMBER(19),
            num_deletes NUMBER(19),
            PRIMARY KEY (migration_uuid, table_name, channel_no)
        );
    ```

1. Create a writer role for fall-forward schema in the fall forward database as follows:

    ```sql
    CREATE ROLE <SCHEMA_NAME>_writer_role;

    BEGIN
        FOR R IN (SELECT owner, object_name FROM all_objects WHERE owner=UPPER('<SCHEMA_NAME>') and object_type ='TABLE' MINUS SELECT owner, table_name from all_nested_tables where owner = UPPER('<SCHEMA_NAME>'))
        LOOP
           EXECUTE IMMEDIATE 'GRANT SELECT, INSERT, UPDATE, DELETE, ALTER on '||R.owner||'."'||R.object_name||'" to  <SCHEMA_NAME>_writer_role';
        END LOOP;
    END;
    /

    DECLARE
       v_sql VARCHAR2(4000);
    BEGIN
        FOR table_rec IN (SELECT table_name FROM all_tables WHERE owner = 'YBVOYAGER_METADATA') LOOP
         v_sql := 'GRANT ALL PRIVILEGES ON YBVOYAGER_METADATA.' || table_rec.table_name || ' TO <SCHEMA_NAME>_writer_role';
          EXECUTE IMMEDIATE v_sql;
        END LOOP;
    END;
    /

    GRANT CREATE ANY SEQUENCE, SELECT ANY SEQUENCE, ALTER ANY SEQUENCE TO <SCHEMA_NAME>_writer_role;
    ```

1. Create a user and grant the preceding writer role to the user as follows:

    ```sql
    CREATE USER YBVOYAGER_FF IDENTIFIED BY password;
    GRANT CONNECT TO YBVOYAGER_FF;
    GRANT <SCHEMA_NAME>_writer_role TO YBVOYAGER_FF;
    ```

1. Set the following variables on the client machine on where yb-voyager is running (Only if yb-voyager is installed on Ubuntu / RHEL) :

    ```sh
    export ORACLE_HOME=/usr/lib/oracle/21/client64
    export LD_LIBRARY_PATH=$ORACLE_HOME/lib
    export PATH=$PATH:$ORACLE_HOME/bin
    ```

## Migrate your database to YugabyteDB

Proceed with schema and data migration using the following steps:

### Export and analyze schema

To begin, export the schema from the source database. Once exported, analyze the schema and apply any necessary manual changes.

#### Export schema

The `yb-voyager export schema` command extracts the schema from the source database, converts it into PostgreSQL format (if the source database is Oracle or MySQL), and dumps the SQL DDL files in the `EXPORT_DIR/schema/*` directories.

{{< note title="Usage for source_db_schema" >}}

The `source_db_schema` argument specifies the schema of the source database.

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
        --source-db-schema <SOURCE_DB_SCHEMA>

```

Refer to [export schema](../../reference/schema-migration/export-schema/) for details about the arguments.

#### Analyze schema

The schema exported in the previous step may not yet be suitable for importing into YugabyteDB. Even though YugabyteDB is PostgreSQL compatible, given its distributed nature, you may need to make minor manual changes to the schema.

The `yb-voyager analyze-schema` command analyses the PostgreSQL schema dumped in the [export schema](#export-schema) step, and prepares a report that lists the DDL statements which need manual changes. An example invocation of the command is as follows:

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

### Import schema

Import the schema using the `yb-voyager import schema` command.

{{< note title="Usage for target_db_schema" >}}

`yb-voyager` imports the source database into the `public` schema of the target database. By specifying `--target-db-schema` argument during import, you can instruct `yb-voyager` to create a non-public schema and use it for the schema/data import.

{{< /note >}}

An example invocation of the command is as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager import schema --export-dir <EXPORT_DIR> \
        --target-db-host <TARGET_DB_HOST> \
        --target-db-user <TARGET_DB_USER> \
        --target-db-password <TARGET_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters..
        --target-db-name <TARGET_DB_NAME> \
        --target-db-schema <TARGET_DB_SCHEMA>
```

Refer to [import schema](../../reference/schema-migration/import-schema/) for details about the arguments.

yb-voyager applies the DDL SQL files located in the `$EXPORT_DIR/schema` directory to the target database. If yb-voyager terminates before it imports the entire schema, you can rerun it by adding the `--ignore-exist` option.

{{< note title="Importing indexes and triggers" >}}

Because the presence of indexes and triggers can slow down the rate at which data is imported, by default `import schema` does not import indexes and triggers. You should complete the data import without creating indexes and triggers. Only after data import is complete, create indexes and triggers using the `import schema` command with an additional `--post-import-data` flag.

{{< /note >}}

### Export and import schema to fall-forward database

Manually, set up the fall-forward database with the same schema as that of the source database with the following considerations:

- The table names on the fall-forward database need to be case insensitive (YB Voyager currently does not support case-sensitivity).
- Do not create indexes and triggers at the schema setup stage, as it will degrade performance of importing data into the fall-forward database. Create them later as described in [Fall-forward switchover](#fall-forward-switchover-optional).

- Disable foreign key constraints and check constraints on the fall-forward database.

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

The export data command first ensures that it exports a snapshot of the data already present on the source database. Next, you start a streaming phase (CDC phase) where you begin capturing new changes made to the data on the source after the migration has started. Some important metrics such as number of events, export rate, and so on will be displayed during the CDC phase similar to the following:

```output
| ---------------------------------------  |  ----------------------------- |
| Metric                                   |                          Value |
| ---------------------------------------  |  ----------------------------- |
| Total Exported Events                    |                         123456 |
| Total Exported Events (Current Run)      |                         123456 |
| Export Rate(Last 3 min)                  |                      22133/sec |
| Export Rate(Last 10 min)                 |                      21011/sec |
| ---------------------------------------  |  ----------------------------- |
```

Note that the CDC phase will start only after a snapshot of the entire interested table-set is completed.
Additionally, the CDC phase is restartable. So, if yb-voyager terminates when data export is in progress, it resumes from its current state after the CDC phase is restarted.

#### Caveats

- Some data types are unsupported. For a detailed list, refer to [datatype mappings](../../reference/datatype-mapping-oracle/).
- For Oracle where sequences are not attached to a column, resume value generation is unsupported.
- `--parallel-jobs` argument (specifies the number of tables to be exported in parallel from the source database at a time) has no effect on live migration.

Refer to [export data](../../reference/data-migration/export-data/) for details about the arguments, and [export data status](../../reference/data-migration/export-data/#export-data-status) to track the status of an export operation.

The options passed to the command are similar to the [`yb-voyager export schema`](#export-schema) command. To export only a subset of the tables, pass a comma-separated list of table names in the `--table-list` argument.

### Import data

After you have successfully imported the schema in the target database, and the CDC phase has started in export data (which you can monitor using the export data status command), you can start importing the data using the yb-voyager import data command as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager import data --export-dir <EXPORT_DIR> \
        --target-db-host <TARGET_DB_HOST> \
        --target-db-user <TARGET_DB_USER> \
        --target-db-password <TARGET_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name <TARGET_DB_NAME> \
        --target-db-schema <TARGET_DB_SCHEMA> \ # Oracle only.
        --parallel-jobs <NUMBER_OF_JOBS>
```

Refer to [import data](../../reference/data-migration/import-data/) for details about the arguments.

For the snapshot exported, yb-voyager splits the data dump files (from the $EXPORT_DIR/data directory) into smaller batches. yb-voyager concurrently ingests the batches such that all nodes of the target YugabyteDB database cluster are used. After the snapshot is imported, a similar approach is employed for the CDC phase, where concurrent batches of change events are applied on the target YugabyteDB database cluster.

Some important metrics such as number of events, ingestion rate, and so on, will be displayed during the CDC phase similar to the following:

```output
| -----------------------------  |  ----------------------------- |
| Metric                         |                          Value |
| -----------------------------  |  ----------------------------- |
| Total Imported events          |                         272572 |
| Events Imported in this Run    |                         272572 |
| Ingestion Rate (last 3 mins)   |               14542 events/sec |
| Ingestion Rate (last 10 mins)  |               14542 events/sec |
| Time taken in this Run         |                      0.83 mins |
| Remaining Events               |                        4727427 |
| Estimated Time to catch up     |                          5m42s |
| -----------------------------  |  ----------------------------- |
```

The entire import process is designed to be _restartable_ if yb-voyager terminates while the data import is in progress. If restarted, the data import resumes from its current state.

{{< note title="Note">}}
The arguments `table-list` and `exclude-table-list` are not supported in live migration.
For details about the arguments, refer to the [arguments table](../../reference/data-migration/import-data/#arguments).
{{< /note >}}

{{< tip title="Importing large datasets" >}}

When importing a very large database, run the import data command in a `screen` session, so that the import is not terminated when the terminal session stops.

If the `yb-voyager import data` command terminates before completing the data ingestion, you can re-run it with the same arguments and the command will resume the data import operation.

{{< /tip >}}

#### Import data status

Run the `yb-voyager import data status --export-dir <EXPORT_DIR>` command to get an overall progress of the data import operation.

### Fall-forward setup

Note that the fall-forward setup is applicable for data migration only (schema migration needs to be done manually).

The fall-forward setup refers to replicating the snapshot data along with the changes exported from the source database to the fall-forward database. The command to start the setup is as follows:

```sh
yb-voyager fall-forward setup --export-dir <EXPORT-DIR> \
--ff-db-host <HOST> \
--ff-db-user <USERNAME> \
--ff-db-password <PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
--ff-db-name <DB-NAME> \
--ff-db-schema <SCHEMA-NAME> \
--parallel-jobs <COUNT>
```

Refer to [fall-forward setup](../../reference/fall-forward/fall-forward-setup/) for details about the arguments.

Similar to [import data](#import-data), during fall-forward:

- The snapshot is first imported, following which, the change events are imported to the fall-forward database.
- Some important metrics such as the number of events, events rate, and so on, are displayed.
- The setup is restartable.

Additionally, when you run the `fall-forward setup` command, the [import data status](#import-data-status) command also shows progress of importing all changes to the fall-forward database. To view overall progress of the data import operation and streaming changes to the fall-forward database, use the following command:

```sh
yb-voyager import data status --export-dir <EXPORT_DIR> --ff-db-password <password>
```

### Archive changes (Optional)

As the migration continuously exports changes on the source database to the `EXPORT-DIR`, the disk utilization continues to grow indefinitely over time. To limit usage of all the disk space, optionally, you can use the `archive changes` command as follows:

```sh
yb-voyager archive changes --export-dir <EXPORT-DIR> --move-to <DESTINATION-DIR> --delete
```

Refer to [archive changes](../../reference/cutover-archive/archive-changes-optional/) for details about the arguments.

{{< note title = "Note" >}}
Make sure to run the archive changes command only after completing [fall-forward setup](#fall-forward-setup). If you run the command before, you may archive some changes before they have been imported to the fall-forward database.
{{< /note >}}

### Cutover to the target

Cutover is the last phase, where you switch your application over from the source database to the target YugabyteDB database.

Keep monitoring the metrics displayed on export data and import data processes. After you notice that the import of events is catching up to the exported events, you are ready to cutover. You can use the "Remaining events" metric displayed in the import data process to help you determine the cutover.

Perform the following steps as part of the cutover process:

1. Quiesce your source database, that is stop application writes.
1. Perform a cutover after the exported events rate ("ingestion rate" in the metrics table) drops to 0 using the following command:

    ```sh
    yb-voyager cutover initiate --export-dir <EXPORT_DIR>
    ```

    Refer to [cutover initiate](../../reference/cutover-archive/cutover/#cutover-initiate) for details about the arguments.

    As part of the cutover process, the following occurs in the background:

    1. The cutover initiate command stops the export data process, followed by the import data process after it has imported all the events to the target YugabyteDB database.

    1. The [fall-forward synchronize](../../reference/fall-forward/fall-forward-synchronize/) command automatically starts synchronizing changes from the target YugabyteDB database to the fall-forward database.
    Note that the [import data](#import-data) process transforms to a `fall-forward synchronize` process, so if it gets terminated for any reason, you need to restart the synchronization using the `fall-forward synchronize` command as suggested in the import data output.

1. Import indexes and triggers using the `import schema` command with an additional `--post-import-data` flag as follows:

    ```sh
    # Replace the argument values with those applicable for your migration.
    yb-voyager import schema --export-dir <EXPORT_DIR> \
            --target-db-host <TARGET_DB_HOST> \
            --target-db-user <TARGET_DB_USER> \
            --target-db-password <TARGET_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
            --target-db-name <TARGET_DB_NAME> \
            --target-db-user <TARGET_DB_USER> \
            --target-db-schema <TARGET_DB_SCHEMA> \
            --post-import-data
    ```

    Refer to [import schema](../../reference/schema-migration/import-schema/) for details about the arguments.

1. Verify your migration. After the schema and data import is complete, the automated part of the database migration process is considered complete. You should manually run validation queries on both the source and target database to ensure that the data is correctly migrated. A sample query to validate the databases can include checking the row count of each table.

    {{< warning title = "Caveat associated with rows reported by import data status" >}}

Suppose you have the following scenario:

- [import data](#import-data) or [import data file](../bulk-data-load/#import-data-files-from-the-local-disk) command fails.
- To resolve this issue, you delete some of the rows from the split files.
- After retrying, the import data command completes successfully.

In this scenario, the [import data status](#import-data-status) command reports an incorrect imported row count because it doesn't take into account the deleted rows.

For more details, refer to the GitHub issue [#360](https://github.com/yugabyte/yb-voyager/issues/360).

    {{< /warning >}}

1. Stop [archive changes](#archive-changes-optional).

### Switch over to the fall forward (Optional)

During this phase, switch your application over from the target YugabyteDB database to the fall-forward database. As this step is optional, perform it _only_ if the target YugabyteDB database is not working as expected.

Keep monitoring the metrics displayed for `fall-forward synchronize` and `fall-forwad setup` processes. After you notice that the import of events to the fall-forward database is catching up to the exported events from the target database, you are ready to switchover. You can use the "Remaining events" metric displayed in the fall-forward setup process to help you determine the switchover.

Perform the following steps as part of the switchover process:

1. Quiesce your target database, that is stop application writes.
1. Perform a switchover after the exported events rate ("ingestion rate" in the metrics table) drops to using the following command:

    ```sh
    yb-voyager fall-forward switchover --export-dir <EXPORT_DIR>
    ```

    Refer to [fall-forward switchover](../../reference/fall-forward/fall-forward-switchover/) for details about the arguments.

    The `fall-forward switchover` command stops the `fall-forward synchronize` process, followed by the `fall-forward setup` process after it has imported all the events to the fall-forward database.

1. Wait for the switchover process to complete. Monitor the status of the switchover process using the following command:

    ```sh
    yb-voyager fall-forward status --export-dir <EXPORT_DIR>
    ```

    Refer to [fall-forward status](../../reference/fall-forward/fall-forward-switchover/#fall-forward-status) for details about the arguments.

1. Setup indexes and triggers to the fall-forward database manually. Also, re-enable the foreign key and check constraints.

1. Verify your migration. After the schema and data import is complete, the automated part of the database migration process is considered complete. You should manually run validation queries on both the source and fall-forward databases to ensure that the data is correctly migrated. A sample query to validate the databases can include checking the row count of each table.

    {{< warning title = "Caveat associated with rows reported by import data status" >}}

Suppose you have a scenario where,

- [import data](#import-data) or [import data file](../bulk-data-load/#import-data-files-from-the-local-disk) command fails.
- To resolve this issue, you delete some of the rows from the split files.
- After retrying, the import data command completes successfully.

In this scenario, [import data status](#import-data-status) command reports incorrect imported row count; because it doesn't take into account the deleted rows.

For more details, refer to the GitHub issue [#360](https://github.com/yugabyte/yb-voyager/issues/360).

    {{< /warning >}}

1. Stop [archive changes](#archive-changes-optional).

{{< note >}}
During `fall-forward synchronize`, yb-voyager creates a CDC stream ID on the target YugabyteDB database to fetch the new changes from the target database which is displayed as part of the `fall-forward synchronize` output. You need to manually delete the stream ID after `fall forward switchover` is completed.
{{< /note >}}

## Limitations

In addition to the Live migration [limitations](../live-migrate/#limitations), the following additional limitations apply to the fall-forward feature:

- Fall-forward is unsupported with a YugabyteDB cluster running on [YugabyteDB Managed](../../../yugabyte-cloud).
- [SSL Connectivity](../../reference/yb-voyager-cli/#ssl-connectivity) is unsupported for export or streaming events from YugabyteDB during `fall-forward synchronize`.
- You need to manually disable constraints on the fall-forward database.
- yb-voyager provides limited datatypes support with YugabyteDB CDC during `fall-forward synchronize` for datatypes such as DECIMAL, and Timestamp.
- You need to manually delete the stream ID of YugabyteDB CDC created by Voyager during `fall-forward synchronize`.
