---
title: Steps to perform live migration with fall-forward using YugabyteDB Voyager
headerTitle: Live migration with fall-forward
linkTitle: Live migration with fall-forward
headcontent: Steps for a live migration with fall-forward using YugabyteDB Voyager
description: Steps to ensure a successful live migration with fall-forward using YugabyteDB Voyager.
menu:
  stable_yugabyte-voyager:
    identifier: live-fall-forward
    parent: migration-types
    weight: 103
type: docs
---

When migrating using YugabyteDB Voyager, it is prudent to have a backup strategy if the new database doesn't work as expected. A fall-forward approach consists of creating a third database (the source-replica database) that is a replica of your original source database.

A fall-forward approach allows you to test the system end-to-end. This workflow is especially important in heterogeneous migration scenarios, in which source and target databases are using different engines.

## Feature availability

Live migration availability varies by the source database type as described in the following table:

| Source database | Feature Maturity |
| :--- | :--- |
| PostgreSQL | {{<tags/feature/ga>}} when using [YugabyteDB Connector](/stable/additional-features/change-data-capture/using-logical-replication/). <br> {{<tags/feature/tp>}} when using [YugabyteDB gRPC Connector](/stable/additional-features/change-data-capture/using-yugabytedb-grpc-replication/debezium-connector-yugabytedb/).|
| Oracle | {{<tags/feature/tp>}} |

## Fall-forward workflow

![fall-forward short](/images/migrate/live-fall-forward-short-new.png)

Before starting a live migration, you set up the source-replica database (via [import data to source-replica](#import-data-to-source-replica)). During migration, yb-voyager replicates the snapshot data along with new changes exported from the source database to the target and source-replica databases, as shown in the following illustration:

![After import data to source-replica](/images/migrate/after-import-data-to-sr-new.png)

At [cutover to target](#cutover-to-the-target), applications stop writing to the source database and start writing to the target YugabyteDB database. After the cutover process is complete, Voyager keeps the source-replica database synchronized with changes from the target YugabyteDB database as shown in the following illustration:

![After cutover](/images/migrate/cutover-to-target-new.png)

Finally, if you need to switch to the source-replica database (because the current YugabyteDB system is not working as expected), you can initiate [cutover to your source-replica](#cutover-to-source-replica-optional).

![After initiate cutover to source-replica](/images/migrate/cutover-to-source-replica-new.png)

The following illustration describes the workflow for live migration using YB Voyager with the fall-forward option.

![Live migration with fall-forward workflow](/images/migrate/live-fall-forward-new.png)

| Phase | Step | Description |
| :---- | :--- | :---|
| PREPARE | [Install voyager](../../install-yb-voyager/#install-yb-voyager) | yb-voyager supports RHEL, CentOS, Ubuntu, and macOS, as well as airgapped and Docker-based installations. |
| | [Prepare source DB](#prepare-the-source-database) | Create a new database user with READ access to all the resources to be migrated. |
| | [Prepare target DB](#prepare-the-target-database) | Deploy a YugabyteDB database and create a user with superuser privileges. |
| | [Prepare source-replica DB](#prepare-source-replica-database) | Deploy a database (a replica of your original source database) and create a user with necessary privileges. |
| ASSESS | [Assess Migration](#assess-migration) | Assess the migration complexity, and get schema changes, data distribution, and cluster sizing recommendations using the `yb-voyager assess-migration` command. |
| SCHEMA | [Export](#export-schema) | Convert the database schema to PostgreSQL format using the `yb-voyager export schema` command. |
| | [Analyze](#analyze-schema) | Generate a _Schema&nbsp;Analysis&nbsp;Report_ using the `yb-voyager analyze-schema` command. The report suggests changes to the PostgreSQL schema to make it appropriate for YugabyteDB. |
| | [Modify](#manually-edit-the-schema) | Using the report recommendations, manually change the exported schema. |
| | [Import](#import-schema) | Import the modified schema to the target YugabyteDB database using the `yb-voyager import schema` command. |
| LIVE MIGRATION | Start | Start the phases: export data, import data to target, followed by import data to source-replica, and archive changes simultaneously. |
| | [Export data](#export-data-from-source) | The export data command first exports a snapshot and then starts continuously capturing changes from the source.|
| | [Import data](#import-data-to-target) | The import data to target command first imports the snapshot, and then continuously applies the exported change events on the target. |
| | [Import data to source-replica](#import-data-to-source-replica) | The import data to source-replica command imports the snapshot, and then continuously applies the exported change events on the source-replica. |
| | [Archive changes](#archive-changes-optional) | Continuously archive migration changes to limit disk utilization. |
| CUTOVER TO TARGET | [Initiate cutover to target](#cutover-to-the-target) | Perform a cutover (stop streaming changes) when the migration process reaches a steady state where you can stop your applications from pointing to your source database, allow all the remaining changes to be applied on the target YugabyteDB database, and then restart your applications pointing to YugabyteDB. |
| | [Wait for cutover to complete](#cutover-to-the-target) | Monitor the wait status using the [cutover status](../../reference/cutover-archive/cutover/#cutover-status) command. |
| | [Verify target DB](#cutover-to-the-target) | Check if the live migration is successful on both the source and the target databases. |
| CUTOVER TO SOURCE&nbsp;REPLICA | [Initiate cutover to source-replica](#cutover-to-source-replica-optional) | Perform a cutover (stop streaming changes) from the target YugabyteDB database to the source-replica database only when the target YugabyteDB database is not working as expected, allow all the change events to be applied to the source-replica database, and then restart your applications pointing to the source-replica database. |
| | [Wait for cutover to complete](#cutover-to-source-replica-optional) | Monitor the wait status using the [cutover status](../../reference/cutover-archive/cutover/#cutover-status) command. |
| | [(Manual)&nbsp;Import&nbsp;indexes and triggers to source-replica DB](#cutover-to-source-replica-optional) | After the snapshot import is complete, import indexes and triggers to the source-replica database manually. |
| | [Verify source-replica DB](#cutover-to-source-replica-optional) | Check if the live migration is successful on both the target and the source-replica databases. |
| END | [End migration](#end-migration) | Clean up the migration information stored in export directory and databases (source, source-replica, and target). |

Before proceeding with migration, ensure that you have completed the following steps:

- [Install yb-voyager](../../install-yb-voyager/#install-yb-voyager).
- Review the [guidelines for your migration](../../known-issues/).
- Review [data modeling](../../../develop/data-modeling) strategies.
- [Prepare the source database](#prepare-the-source-database).
- [Prepare the target database](#prepare-the-target-database).

## Prepare the source database

Create a new database user, and assign the necessary user permissions.

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#oracle" class="nav-link active" id="oracle-tab" data-bs-toggle="tab"
      role="tab" aria-controls="oracle" aria-selected="true">
      <i class="icon-oracle" aria-hidden="true"></i>
      Oracle
    </a>
  </li>
  <li >
    <a href="#pg" class="nav-link" id="pg-tab" data-bs-toggle="tab"
      role="tab" aria-controls="pg" aria-selected="false">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL
    </a>
  </li>
</ul>
<div class="tab-content">
  <div id="oracle" class="tab-pane fade show active" role="tabpanel" aria-labelledby="oracle-tab">

Live migration is {{<tags/feature/tp>}} for Oracle source databases.

{{< tabpane text=true >}}

  {{% tab header="Standalone Oracle Container Database" %}}

1. Ensure that your database log_mode is `archivelog` as follows:

    ```sql
    SELECT LOG_MODE FROM V$DATABASE;
    ```

    ```output
    LOG_MODE
    ------------
    ARCHIVELOG
    ```

    If log_mode is NOARCHIVELOG (that is, not enabled), run the following command:

    ```sql
    sqlplus /nolog
    SQL>alter system set db_recovery_file_dest_size = 10G;
    SQL>alter system set db_recovery_file_dest = '<oracle_path>/oradata/recovery_area' scope=spfile;
    SQL> connect / as sysdba
    SQL> Shutdown immediate
    SQL> Startup mount
    SQL> Alter database archivelog;
    SQL> Alter database open;
    ```

1. Create the tablespaces as follows:

    1. Connect to Pluggable database (PDB) as sysdba and run the following command:

        ```sql
        CREATE TABLESPACE logminer_tbs DATAFILE '/opt/oracle/oradata/ORCLCDB/ORCLPDB1/logminer_tbs.dbf'
          SIZE 25M REUSE AUTOEXTEND ON MAXSIZE UNLIMITED;
        ```

    1. Connect to Container database (CDB) as sysdba and run the following command:

        ```sql
        CREATE TABLESPACE logminer_tbs DATAFILE '/opt/oracle/oradata/ORCLCDB/logminer_tbs.dbf'
          SIZE 25M REUSE AUTOEXTEND ON MAXSIZE UNLIMITED;
        ```

1. Run the following commands from CDB as sysdba:

    ```sql
    CREATE USER c##ybvoyager IDENTIFIED BY password
      DEFAULT TABLESPACE logminer_tbs
      QUOTA UNLIMITED ON logminer_tbs
      CONTAINER=ALL;

    GRANT CREATE SESSION TO c##ybvoyager CONTAINER=ALL;
    GRANT SET CONTAINER TO c##ybvoyager CONTAINER=ALL;
    GRANT SELECT ON V_$DATABASE to c##ybvoyager CONTAINER=ALL;
    GRANT FLASHBACK ANY TABLE TO c##ybvoyager CONTAINER=ALL;
    GRANT SELECT ANY TABLE TO c##ybvoyager CONTAINER=ALL;
    GRANT SELECT_CATALOG_ROLE TO c##ybvoyager CONTAINER=ALL;
    GRANT EXECUTE_CATALOG_ROLE TO c##ybvoyager CONTAINER=ALL;
    GRANT SELECT ANY TRANSACTION TO c##ybvoyager CONTAINER=ALL;
    GRANT LOGMINING TO c##ybvoyager CONTAINER=ALL;

    GRANT CREATE TABLE TO c##ybvoyager CONTAINER=ALL;
    GRANT LOCK ANY TABLE TO c##ybvoyager CONTAINER=ALL;
    GRANT CREATE SEQUENCE TO c##ybvoyager CONTAINER=ALL;

    GRANT EXECUTE ON DBMS_LOGMNR TO c##ybvoyager CONTAINER=ALL;
    GRANT EXECUTE ON DBMS_LOGMNR_D TO c##ybvoyager CONTAINER=ALL;

    GRANT SELECT ON V_$LOG TO c##ybvoyager CONTAINER=ALL;
    GRANT SELECT ON V_$LOG_HISTORY TO c##ybvoyager CONTAINER=ALL;
    GRANT SELECT ON V_$LOGMNR_LOGS TO c##ybvoyager CONTAINER=ALL;
    GRANT SELECT ON V_$LOGMNR_CONTENTS TO c##ybvoyager CONTAINER=ALL;
    GRANT SELECT ON V_$LOGMNR_PARAMETERS TO c##ybvoyager CONTAINER=ALL;
    GRANT SELECT ON V_$LOGFILE TO c##ybvoyager CONTAINER=ALL;
    GRANT SELECT ON V_$ARCHIVED_LOG TO c##ybvoyager CONTAINER=ALL;
    GRANT SELECT ON V_$ARCHIVE_DEST_STATUS TO c##ybvoyager CONTAINER=ALL;
    GRANT SELECT ON V_$TRANSACTION TO c##ybvoyager CONTAINER=ALL;

    GRANT SELECT ON V_$MYSTAT TO c##ybvoyager CONTAINER=ALL;
    GRANT SELECT ON V_$STATNAME TO c##ybvoyager CONTAINER=ALL;
    ```

1. Enable supplemental logging in the database as follows:

    ```sql
    ALTER DATABASE ADD SUPPLEMENTAL LOG DATA;
    ALTER DATABASE ADD SUPPLEMENTAL LOG DATA (ALL) COLUMNS;
    ```

  {{% /tab %}}

  {{% tab header="RDS Oracle" %}}

**Note** that the following steps assume you're using SQL*Plus or a compatible Oracle client that supports `EXEC`. If your client doesn't support `EXEC`, use the standard SQL CALL syntax instead.

1. Ensure that your database log_mode is `archivelog` as follows:

    ```sql
    SELECT LOG_MODE FROM V$DATABASE;
    ```

    ```output
    LOG_MODE
    ------------
    ARCHIVELOG
    ```

    If log_mode is NOARCHIVELOG (that is, not enabled), run the following command:

    ```sql
    exec rdsadmin.rdsadmin_util.set_configuration('archivelog retention hours',24);
    ```

1. Connect to your database as an admin user, and create the tablespaces as follows:

    ```sql
    CREATE TABLESPACE logminer_tbs DATAFILE SIZE 25M AUTOEXTEND ON MAXSIZE UNLIMITED;
    ```

1. Run the following commands connected to the admin or privileged user:

    ```sql
    CREATE USER ybvoyager IDENTIFIED BY password
      DEFAULT TABLESPACE logminer_tbs
      QUOTA UNLIMITED ON logminer_tbs;

    GRANT CREATE SESSION TO YBVOYAGER;
    begin rdsadmin.rdsadmin_util.grant_sys_object(
          p_obj_name  => 'V_$DATABASE',
          p_grantee   => 'YBVOYAGER',
          p_privilege => 'SELECT');
    end;
    /

    GRANT FLASHBACK ANY TABLE TO YBVOYAGER;
    GRANT SELECT ANY TABLE TO YBVOYAGER;
    GRANT SELECT_CATALOG_ROLE TO YBVOYAGER;
    GRANT EXECUTE_CATALOG_ROLE TO YBVOYAGER;
    GRANT SELECT ANY TRANSACTION TO YBVOYAGER;
    GRANT LOGMINING TO YBVOYAGER;

    GRANT CREATE TABLE TO YBVOYAGER;
    GRANT LOCK ANY TABLE TO YBVOYAGER;
    GRANT CREATE SEQUENCE TO YBVOYAGER;


    begin rdsadmin.rdsadmin_util.grant_sys_object(
          p_obj_name => 'DBMS_LOGMNR',
          p_grantee => 'YBVOYAGER',
          p_privilege => 'EXECUTE',
          p_grant_option => true);
    end;
    /

    begin rdsadmin.rdsadmin_util.grant_sys_object(
          p_obj_name => 'DBMS_LOGMNR_D',
          p_grantee => 'YBVOYAGER',
          p_privilege => 'EXECUTE',
          p_grant_option => true);
    end;
    /

    begin rdsadmin.rdsadmin_util.grant_sys_object(
          p_obj_name  => 'V_$LOG',
          p_grantee   => 'YBVOYAGER',
          p_privilege => 'SELECT');
    end;
    /

    begin
        rdsadmin.rdsadmin_util.grant_sys_object(
            p_obj_name  => 'V_$LOG_HISTORY',
            p_grantee   => 'YBVOYAGER',
            p_privilege => 'SELECT');
    end;
    /

    begin
        rdsadmin.rdsadmin_util.grant_sys_object(
            p_obj_name  => 'V_$LOGMNR_LOGS',
            p_grantee   => 'YBVOYAGER',
            p_privilege => 'SELECT');
    end;
    /

    begin
        rdsadmin.rdsadmin_util.grant_sys_object(
            p_obj_name  => 'V_$LOGMNR_CONTENTS',
            p_grantee   => 'YBVOYAGER',
            p_privilege => 'SELECT');
    end;
    /

    begin
        rdsadmin.rdsadmin_util.grant_sys_object(
            p_obj_name  => 'V_$LOGMNR_PARAMETERS',
            p_grantee   => 'YBVOYAGER',
            p_privilege => 'SELECT');
    end;
    /

    begin
        rdsadmin.rdsadmin_util.grant_sys_object(
            p_obj_name  => 'V_$LOGFILE',
            p_grantee   => 'YBVOYAGER',
            p_privilege => 'SELECT');
    end;
    /

    begin
        rdsadmin.rdsadmin_util.grant_sys_object(
            p_obj_name  => 'V_$ARCHIVED_LOG',
            p_grantee   => 'YBVOYAGER',
            p_privilege => 'SELECT');
    end;
    /

    begin
        rdsadmin.rdsadmin_util.grant_sys_object(
            p_obj_name  => 'V_$ARCHIVE_DEST_STATUS',
            p_grantee   => 'YBVOYAGER',
            p_privilege => 'SELECT');
    end;
    /

    begin
        rdsadmin.rdsadmin_util.grant_sys_object(
            p_obj_name  => 'V_$TRANSACTION',
            p_grantee   => 'YBVOYAGER',
            p_privilege => 'SELECT');
    end;
    /

    begin
        rdsadmin.rdsadmin_util.grant_sys_object(
            p_obj_name  => 'V_$MYSTAT',
            p_grantee   => 'YBVOYAGER',
            p_privilege => 'SELECT');
    end;
    /

    begin
        rdsadmin.rdsadmin_util.grant_sys_object(
            p_obj_name  => 'V_$STATNAME',
            p_grantee   => 'YBVOYAGER',
            p_privilege => 'SELECT');
    end;
    /
    ```

1. Enable supplemental logging in the database as follows:

    ```sql
    exec rdsadmin.rdsadmin_util.alter_supplemental_logging('ADD');

    begin
        rdsadmin.rdsadmin_util.alter_supplemental_logging(
            p_action => 'ADD',
            p_type   => 'ALL');
    end;
    /
    ```

  {{% /tab %}}

{{< /tabpane >}}

If you want yb-voyager to connect to the source database over SSL, refer to [SSL Connectivity](../../reference/yb-voyager-cli/#ssl-connectivity).

{{< note title="Connecting to Oracle instances" >}}

You can use only one of the following arguments in the `source` parameter (configuration file) or CLI flag to connect to your Oracle instance:

| `source` section parameters (configuration file)  | CLI Flag | Description |
|---|---|---|
|`db-schema`|`--source-db-schema`|Schema name of the source database.|
|`oracle-db-sid`|`--oracle-db-sid`|Oracle System Identifier you can use while exporting data from Oracle instances.|
|`oracle-tns-alias`|`--oracle-tns-alias`|TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server.|

{{< /note >}}

  </div>
  <div id="pg" class="tab-pane fade" role="tabpanel" aria-labelledby="pg-tab">

Live migration for PostgreSQL source database (using YugabyteDB Connector) is {{<tags/feature/ga>}}.

{{< tabpane text=true >}}

  {{% tab header="Standalone PostgreSQL" %}}

1. yb-voyager requires `wal_level` to be logical. You can check this using following the steps:

    1. Run the command `SHOW wal_level` on the database to check the value.

    1. If the value is anything other than logical, run the command `SHOW config_file` to know the path of your configuration file.

    1. Modify the configuration file by uncommenting the parameter `wal_level` and set the value to logical.

    1. Restart PostgreSQL.

1. Create user `ybvoyager` for the migration using the following command:

    ```sql
    CREATE USER ybvoyager PASSWORD 'password';
    ```

1. Grant permissions for migration. Use the [yb-voyager-pg-grant-migration-permissions.sql](../../reference/yb-voyager-pg-grant-migration-permissions/) script (in `/opt/yb-voyager/guardrails-scripts/` or, for brew, check in `$(brew --cellar)/yb-voyager@<voyagerversion>/<voyagerversion>`).

    The script does the following:

    - Grants permissions to the migration user (`ybvoyager`). This script provides two options for granting permissions:

        - Transfer ownership: Transfers ownership of all tables in the specified schemas to the specified replication group, and adds the original table owners and the migration user to that group.
        - Grant owner role: Grants the original table owner role of each table to the migration user, without transferring table ownership.

    - Sets [Replica identity](/stable/additional-features/change-data-capture/using-logical-replication/yugabytedb-connector/#replica-identity) FULL on all tables in the specified schemas.

    Use the script to grant the required permissions as follows:

    ```sql
    psql -h <host> \
          -d <database> \
          -U <username> \ # A superuser or a privileged user with enough permissions to grant privileges
          -v voyager_user='ybvoyager' \
          -v schema_list='<comma_separated_schema_list>' \
          -v is_live_migration=1 \
          -v is_live_migration_fall_back=0 \
          -v replication_group='<replication_group>' \
          -f <path_to_the_script>
    ```

    The `ybvoyager` user can now be used for migration.

  {{% /tab %}}

  {{% tab header="RDS PostgreSQL" %}}

1. yb-voyager requires `wal_level` to be logical. This is controlled by a database parameter `rds.logical_replication` which needs to be set to 1. You can check this using following the steps:

    1. Run the command `SHOW rds.logical_replication` on the database to check whether the parameter is set.

    1. If the parameter is not set, you can change the parameter value to 1 from the RDS console of the database; navigate to **Configuration** > **Parameter group** > `rds.logical_replication`.

    1. If the `rds.logical_replication` errors out (after the change), create a new parameter group with the value as 1, and assign it to the database instance from the **Modify** option on the RDS console.

    1. Restart RDS.

1. Create user `ybvoyager` for the migration using the following command:

    ```sql
    CREATE USER ybvoyager PASSWORD 'password';
    ```

1. Grant permissions for migration. Use the [yb-voyager-pg-grant-migration-permissions.sql](../../reference/yb-voyager-pg-grant-migration-permissions/) script (in `/opt/yb-voyager/guardrails-scripts/` or, for brew, check in `$(brew --cellar)/yb-voyager@<voyagerversion>/<voyagerversion>`).

    The script does the following:

    - Grants permissions to the migration user (`ybvoyager`). This script provides two options for granting permissions:

        - Transfer ownership: Transfers ownership of all tables in the specified schemas to the specified replication group, and adds the original table owners and the migration user to that group.
        - Grant owner role: Grants the original table owner role of each table to the migration user, without transferring table ownership.

    - Sets [Replica identity](/stable/additional-features/change-data-capture/using-logical-replication/yugabytedb-connector/#replica-identity) FULL on all tables in the specified schemas.

    Use the script to grant the required permissions as follows:

    ```sql
    psql -h <host> \
          -d <database> \
          -U <username> \ # A superuser or a privileged user with enough permissions to grant privileges
          -v voyager_user='ybvoyager' \
          -v schema_list='<comma_separated_schema_list>' \
          -v is_live_migration=1 \
          -v is_live_migration_fall_back=0 \
          -v replication_group='<replication_group>' \
          -f <path_to_the_script>
    ```

    The `ybvoyager` user can now be used for migration.

  {{% /tab %}}

{{< /tabpane >}}

If you want yb-voyager to connect to the source database over SSL, refer to [SSL Connectivity](../../reference/yb-voyager-cli/#ssl-connectivity).

</div>

## Prepare the target database

If you plan to use the [YugabyteDB gRPC Connector](../../../additional-features/change-data-capture/using-yugabytedb-grpc-replication/debezium-connector-yugabytedb/), ensure that the TServer (9100) and Master (7100) ports are open on the target YugabyteDB cluster. These ports are required during the `export data from target` phase (after the `cutover to target` step) to initiate Change Data Capture (CDC) from the target and stream ongoing changes.

However, if you are using the [YugabyteDB Connector](../../../additional-features/change-data-capture/using-logical-replication/yugabytedb-connector/), no additional port configuration or setup is required.

Prepare your target YugabyteDB database cluster by creating a database, and a user for your cluster.

{{<note title="Important">}}

Add the following flags to the cluster before starting migration, and revert them after the migration is complete.

For the target YugabyteDB versions `2.18.5.1` and `2.18.6.0` (or later minor versions), set the following flag:

```sh
ysql_pg_conf_csv = yb_max_query_layer_retries=0
```

For all the other target YugabyteDB versions, set the following flags:

```sh
ysql_max_read_restart_attempts = 0
ysql_max_write_restart_attempts = 0
```

Turn off the [read-committed](../../../explore/transactions/isolation-levels/#read-committed-isolation) isolation level on the target YugabyteDB cluster during the migration.

{{</note>}}

### Create the target database

Create the target YugabyteDB database in your YugabyteDB cluster. The database name can be the same or different from the source database name.

If you don't provide the target YugabyteDB database name during import, yb-voyager assumes the target YugabyteDB database name is `yugabyte`. To specify the target YugabyteDB database name during import, use the `db-name` parameter under the `target` section of the config file or `--target-db-name` CLI flag with the `yb-voyager import` commands.

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

- For YugabyteDB Aeon, create a user with [`yb_superuser`](../../../yugabyte-cloud/cloud-secure-clusters/cloud-users/#admin-and-yb-superuser) role and grant replication role using the following command:

     ```sql
     CREATE USER ybvoyager PASSWORD 'password';
     GRANT yb_superuser TO ybvoyager;
     SELECT enable_replication_role('ybvoyager');
     GRANT CREATE on DATABASE <target_database_name> to ybvoyager;

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

## Prepare source-replica database

Perform the following steps to prepare your source-replica database:

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#oracle-source-replica" class="nav-link active" id="oracle-tab" data-bs-toggle="tab"
      role="tab" aria-controls="oracle-source-replica" aria-selected="true">
      <i class="icon-oracle" aria-hidden="true"></i>
      Oracle
    </a>
  </li>
  <li >
    <a href="#pg-source-replica" class="nav-link" id="pg-tab" data-bs-toggle="tab"
      role="tab" aria-controls="pg-source-replica" aria-selected="false">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL
    </a>
  </li>
</ul>
<div class="tab-content">
  <div id="oracle-source-replica" class="tab-pane fade show active" role="tabpanel" aria-labelledby="oracle-tab">

1. Create `ybvoyager_metadata` schema or user, and tables as follows:

    ```sql
    CREATE USER ybvoyager_metadata IDENTIFIED BY "password";
    GRANT CONNECT, RESOURCE TO ybvoyager_metadata;
    ALTER USER ybvoyager_metadata QUOTA UNLIMITED ON USERS;

    --upgraded to ybvoyager_import_data_batches_metainfo_v3 post v1.6
    CREATE TABLE ybvoyager_metadata.ybvoyager_import_data_batches_metainfo_v3 (
               migration_uuid VARCHAR2(36),
               data_file_name VARCHAR2(4000),
               batch_number NUMBER(10),
               schema_name VARCHAR2(4000),
               table_name VARCHAR2(4000),
               rows_imported NUMBER(19),
               PRIMARY KEY (migration_uuid, data_file_name, batch_number, schema_name, table_name)
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
            table_name VARCHAR2(4000),
            channel_no INT,
            total_events NUMBER(19),
            num_inserts NUMBER(19),
            num_updates NUMBER(19),
            num_deletes NUMBER(19),
            PRIMARY KEY (migration_uuid, table_name, channel_no)
        );
    ```

1. Create a writer role for source-replica schema in the source-replica database, and assign privileges for `ybvoyager_metadata` as follows:

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

1. If yb-voyager is installed on Ubuntu or RHEL, set the following variables on the client machine where yb-voyager is running:

    ```sh
    export ORACLE_HOME=/usr/lib/oracle/21/client64
    export LD_LIBRARY_PATH=$ORACLE_HOME/lib
    export PATH=$PATH:$ORACLE_HOME/bin
    ```

  </div>
  <div id="pg-source-replica" class="tab-pane fade" role="tabpanel" aria-labelledby="pg-tab">

{{< tabpane text=true >}}

  {{% tab header="Standalone PostgreSQL" %}}

Create a user `ybvoyager_ff` for the migration with superuser privileges as follows:

```sql
CREATE USER ybvoyager_ff with password 'password' superuser;
```

  {{% /tab %}}

  {{% tab header="RDS PostgreSQL" %}}

1. Create user `ybvoyager_ff` for the migration as follows:

    ```sql
    CREATE USER ybvoyager_ff with password 'password';
    GRANT rds_superuser to ybvoyager_ff;
    ```

1. Grant `CREATE` privilege on the source database to `ybvoyager_ff` as follows:

    ```sql
    GRANT CREATE ON DATABASE <database_name> TO ybvoyager_ff;
    ```

  {{% /tab %}}

{{< /tabpane >}}

</div>

## Set up a configuration file

You can use a [configuration file](../../reference/configuration-file/) to specify the parameters required when running Voyager commands (v2025.6.2 or later).

To get started, copy the `live-migration-with-fall-forward.yaml` template configuration file from one of the following locations to the migration folder you created (for example, `$HOME/my-migration/`):

{{< tabpane text=true >}}
  {{% tab header="Linux (apt/yum/airgapped)" lang="linux" %}}

```bash
/opt/yb-voyager/config-templates/live-migration-with-fall-forward.yaml
```

  {{% /tab %}}
  {{% tab header="MacOS (Homebrew)" lang="macos" %}}

```bash
$(brew --cellar)/yb-voyager@<voyager-version>/<voyager-version>/config-templates/live-migration-with-fall-forward.yaml
```

Replace `<voyager-version>` with your installed Voyager version, for example, `2025.5.2`.

  {{% /tab %}}
{{< /tabpane >}}

Set the export-dir, source, target, and source-replica arguments in the configuration file:

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

source-replica:
  db-type: <source-replica-db-type>
  db-host: <source-replica-db-host>
  db-port: <source-replica-db-port>
  db-name: <source-replica-db-name>
  db-schema: <source-replica-db-schema> # Not applicable for MySQL
  db-user: <source-replica-db-user>
  db-password: <source-replica-db-password> # Enclose the password in single quotes if it contains special characters.
```

Refer to the [live-migration-with-fall-forward.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/live-migration-with-fall-forward.yaml) template for more information on the available global, source, and target configuration parameters supported by Voyager.

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

**For Oracle migrations**:

- `source-db-schema` (CLI) or `db-schema` (configuration file) can take only one schema name and you can migrate _only one_ schema at a time.

The `db-schema` key inside the `source` section parameters (configuration file), or the `--source-db-schema` flag (CLI), is used to specify the schema(s) to migrate from the source database.

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

```sh
yb-voyager export schema --config-file <path-to-config-file>
```

You can specify additional `export schema` parameters in the `export-schema` section of the configuration file. For more details, refer to the [live-migration-with-fall-forward.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/live-migration-with-fall-forward.yaml) template.

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

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

  {{% /tab %}}

{{< /tabpane >}}

Refer to [export schema](../../reference/schema-migration/export-schema/) for more information.

Note that if the source database is PostgreSQL and you haven't already run `assess-migration`, the schema is also assessed and a migration assessment report is generated.

#### Analyze schema

The schema exported in the previous step may not yet be suitable for importing into YugabyteDB. Even though YugabyteDB is PostgreSQL compatible, given its distributed nature, you may need to make minor manual changes to the schema.

The `yb-voyager analyze-schema` command analyses the PostgreSQL schema dumped in the [export schema](#export-schema) step, and prepares a report that lists the DDL statements which need manual changes.

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

Add output format argument to the config file:

```yaml
...
analyze-schema:
  output-format: <FORMAT>
...
```

Then run the command as follows:

```sh
yb-voyager analyze-schema --config-file <path-to-config-file>
```

You can specify additional `analyze-schema` parameters in the `analyze-schema` section of the configuration file. For more details, refer to the [live-migration-with-fall-forward.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/live-migration-with-fall-forward.yaml) template.

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager analyze-schema --export-dir <EXPORT_DIR> --output-format <FORMAT>
```

  {{% /tab %}}

{{< /tabpane >}}

The above command generates a report file under the `EXPORT_DIR/reports/` directory.

Refer to [analyze schema](../../reference/schema-migration/analyze-schema/) for more information.

#### Manually edit the schema

Fix all the issues listed in the generated schema analysis report by manually editing the SQL DDL files from the `EXPORT_DIR/schema/*`.

After making the manual changes, re-run the `yb-voyager analyze-schema` command. This generates a fresh report using your changes. Repeat these steps until the generated report contains no issues.

To learn more about modelling strategies using YugabyteDB, refer to [Data modeling](../../../develop/data-modeling).

{{< note title="Manual schema changes" >}}

Include the primary key definition in the `CREATE TABLE` statement. Primary Key cannot be added to a partitioned table using the `ALTER TABLE` statement.

{{< /note >}}

Refer to the [Manual review guideline](../../known-issues/) for a detailed list of limitations and suggested workarounds associated with the source databases when migrating to YugabyteDB Voyager.

### Import schema

Import the schema using the `yb-voyager import schema` command.

The `db-schema` key inside the `target` section parameters (configuration file), or the `--target-db-schema` flag (CLI), is used to specify the schema in the target YugabyteDB database where the source schema will be imported.`yb-voyager` imports the source database into the `public` schema of the target YugabyteDB database. By specifying this argument during import, you can instruct `yb-voyager` to create a non-public schema and use it for the schema/data import.

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

```sh
yb-voyager import schema --config-file <path-to-config-file>
```

You can specify additional `import schema` parameters in the `import-schema` section of the configuration file. For more details, refer to the [live-migration-with-fall-forward.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/live-migration-with-fall-forward.yaml) template.

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager import schema --export-dir <EXPORT_DIR> \
        --target-db-host <TARGET_DB_HOST> \
        --target-db-user <TARGET_DB_USER> \
        --target-db-password <TARGET_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters..
        --target-db-name <TARGET_DB_NAME> \
        --target-db-schema <TARGET_DB_SCHEMA>
```

  {{% /tab %}}

{{< /tabpane >}}

Refer to [import schema](../../reference/schema-migration/import-schema/) for more information.

{{< note title="NOT VALID constraints are not imported" >}}

Currently, `import schema` does not import NOT VALID constraints exported from source, because this could lead to constraint violation errors during the import if the source contains the data that is violating the constraint.

To add the constraints back, you run the `finalize-schema-post-data-import` command after data import. See [Cutover to the target](#cutover-to-the-target).

{{< /note >}}

yb-voyager applies the DDL SQL files located in the `schema` sub-directory of the [export directory](#create-an-export-directory) to the target YugabyteDB database. If yb-voyager terminates before it imports the entire schema, you can rerun it by adding the `ignore-exist` argument (configuration file), or using the `--ignore-exist` flag (CLI).

### Export and import schema to source-replica database

Manually, set up the source-replica database with the same schema as that of the source database with the following considerations:

- Do not create indexes and triggers at the schema setup stage, as it will degrade performance of importing data into the source-replica database. Create them later as described in [cutover to source-replica](#cutover-to-source-replica-optional).

- For Oracle migrations, disable foreign key constraints and check constraints on the source-replica database.

### Export data from source

Begin exporting data from the source database into the `EXPORT_DIR/data` directory using the `yb-voyager export data from source` command:

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

```sh
yb-voyager export data from source --config-file <path-to-config-file>
```

You can specify additional `export data` parameters in the `export-data` section of the configuration file. For more details, refer to the [live-migration-with-fall-forward.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/live-migration-with-fall-forward.yaml) template.

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager export data from source --export-dir <EXPORT_DIR> \
        --source-db-type <SOURCE_DB_TYPE> \
        --source-db-host <SOURCE_DB_HOST> \
        --source-db-user <SOURCE_DB_USER> \
        --source-db-password <SOURCE_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
        --source-db-name <SOURCE_DB_NAME> \
        --source-db-schema <SOURCE_DB_SCHEMA> \
        --export-type snapshot-and-changes
```

  {{% /tab %}}

{{< /tabpane >}}

{{< note title="PostgreSQL and parallel jobs" >}}
For PostgreSQL, make sure that no other processes are running on the source database that can try to take locks; with more than one parallel job, Voyager will not be able to take locks to dump the data.
{{< /note >}}

{{< note title= "Migrating source databases with large row sizes" >}}
If a table's row size on the source database is too large, and exceeds the default [RPC message size](../../../reference/configuration/all-flags-yb-master/#rpc-max-message-size), import data will fail with the error `ERROR: Sending too long RPC message..`. So, you need to migrate those tables separately after removing the large rows.
{{< /note >}}

The export data from source command first ensures that it exports a snapshot of the data already present on the source database. Next, you start a streaming phase (CDC phase) where you begin capturing new changes made to the data on the source after the migration has started. Some important metrics such as number of events, export rate, and so on will be displayed during the CDC phase similar to the following:

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

{{<note title="Important">}}
yb-voyager creates a replication slot in the source database where disk space can be used up rapidly. To avoid this, execute the [Cutover to the target](#cutover-to-the-target) or [End Migration](#end-migration) steps to delete the replication slot. If you choose to skip these steps, then you must delete the replication slot manually to reduce disk usage.
{{</note>}}

#### Caveats

- Some data types are unsupported. For a detailed list, refer to [datatype mappings](../../reference/datatype-mapping-oracle/).
- For Oracle where sequences are not attached to a column, resume value generation is unsupported.
- `parallel-jobs` parameter under the `export-data-from-source` section (configuration file) or the `--parallel-jobs` CLI argument (specifies the number of tables to be exported in parallel from the source database at a time) has no effect on live migration.

Refer to [export data](../../reference/data-migration/export-data/#export-data) for more information on the use of the command.

The options passed to the command are similar to the [`yb-voyager export schema`](#export-schema) command. To export only a subset of tables, provide a comma-separated list of table names using the `table-list` argument (configuration file), or pass it via the `--table-list` flag (CLI).

#### get data-migration-report

To get a consolidated report of the overall progress of data migration concerning all the databases involved (source and target), you can run the `yb-voyager get data-migration-report` command. You specify the `<EXPORT_DIR>` to push data in using `export-dir` parameter (configuration file), or `--export-dir` flag (CLI).

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

```sh
yb-voyager get data-migration-report --config-file <path-to-config-file>
```

You can specify additional `get data-migration-report` parameters in the `get-data-migration-report` section of the configuration file. For more details, refer to the [live-migration-with-fall-forward.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/live-migration-with-fall-forward.yaml) template.

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

```sh
 yb-voyager get data-migration-report --export-dir <EXPORT_DIR>
```

{{% /tab %}}

{{< /tabpane >}}

Refer to [get data-migration-report](../../reference/data-migration/export-data/#get-data-migration-report) for more information.

### Import data to target

After you have successfully imported the schema in the target YugabyteDB database, and the CDC phase has started in export data from source (which you can monitor using the get data-migration-report command), you can start importing the data using the `yb-voyager import data to target` command:

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

```sh
yb-voyager import data to target --config-file <path-to-config-file>
```

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager import data to target --export-dir <EXPORT_DIR> \
        --target-db-host <TARGET_DB_HOST> \
        --target-db-user <TARGET_DB_USER> \
        --target-db-password <TARGET_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name <TARGET_DB_NAME> \
        --target-db-schema <TARGET_DB_SCHEMA> \ # MySQL and Oracle only.
```

  {{% /tab %}}

{{< /tabpane >}}

Refer to [import data](../../reference/data-migration/import-data/) for more information.

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

When importing a very large database, run the import data to target command in a `screen` session, so that the import is not terminated when the terminal session stops.

If the `yb-voyager import data to target` command terminates before completing the data ingestion, you can re-run it with the same arguments and the command will resume the data import operation.

{{< /tip >}}

##### Migrating Oracle source databases with large row sizes

When migrating from Oracle source, when the snapshot import process, the default row size limit for data import is 32MB. If a row exceeds this limit but is smaller than the `batch-size * max-row-size`, you can increase the limit for the import data process by setting the `csv-reader-max-buffer-size-bytes` parameter in the `import-data-to-target` (configuration file) or export the environment variable `CSV_READER_MAX_BUFFER_SIZE_BYTES` with the value.

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

```yaml
import-data-to-target:
  csv-reader-max-buffer-size-bytes: <MAX_ROW_SIZE_IN_BYTES>
```

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

```sh
export CSV_READER_MAX_BUFFER_SIZE_BYTES = <MAX_ROW_SIZE_IN_BYTES>
```

{{< /tab >}}

{{< /tabpane >}}

#### get data-migration-report

To get a consolidated report of the overall progress of data migration concerning all the databases involved (source or target), you can run the `yb-voyager get data-migration-report` command. You specify the `<EXPORT_DIR>` to push data in using `export-dir` parameter (configuration file), or `--export-dir` flag (CLI).

Run the command as follows:

{{< tabpane text=true >}}
  {{% tab header="Config file" lang="config" %}}

```sh
yb-voyager get data-migration-report --config-file <path-to-config-file>
```

  {{% /tab %}}
{{% tab header="CLI" lang="cli" %}}

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager get data-migration-report --export-dir <EXPORT_DIR> --target-db-password <TARGET_DB_PASSWORD>
```

  {{% /tab %}}
{{< /tabpane >}}

Refer to [get data-migration-report](../../reference/data-migration/import-data/#get-data-migration-report) for more information.

### Import data to source-replica

Note that the import data to source-replica is applicable for data migration only (schema migration needs to be done manually).

The import data to source-replica refers to replicating the snapshot data along with the changes exported from the source database to the source-replica database. The command to start the import with required arguments is as follows:

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

```sh
yb-voyager import data to source-replica --config-file <path-to-config-file>
```

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager import data to source-replica --export-dir <EXPORT-DIR> \
        --source-replica-db-host <HOST> \
        --source-replica-db-user <USERNAME> \
        --source-replica-db-password <PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
        --source-replica-db-name <DB-NAME> \
        --source-replica-db-schema <SCHEMA-NAME> \
        --parallel-jobs <COUNT>
```

  {{% /tab %}}

{{< /tabpane >}}

Refer to [import data to source-replica](../../reference/data-migration/import-data/#import-data-to-source-replica) for more information.

Similar to [import data to target](#import-data-to-target), during `import data to source-replica`:

- The snapshot is first imported, following which, the change events are imported to the source-replica database.
- Some important metrics such as the number of events, events rate, and so on, are displayed.
- You can restart the command if it fails for any reason.

Additionally, when you run the `import data to source-replica` command, the [get data-migration-report](#get-data-migration-report) command also shows progress of importing all changes to the source-replica database. To view overall progress of the data import operation and streaming changes to the source-replica database, use the following command with required arguments:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager get data-migration-report --export-dir <EXPORT_DIR> \
        --target-db-password <TARGET_DB_PASSWORD> \
        --source-replica-db-password <SOURCE_REPLICA_DB_PASSWORD>
```

### Archive changes (Optional)

As the migration continuously exports changes on the source database to the `EXPORT-DIR`, disk use continues to grow. To prevent the disk from filling up, you can optionally use the `archive changes` command:

{{< note title = "Note" >}}
Make sure to run the archive changes command only after completing [import data to source-replica](#import-data-to-source-replica). If you run the command before, you may archive some changes before they have been imported to the source-replica database.
{{< /note >}}

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

```sh
yb-voyager archive changes --config-file <path-to-config-file>
```

You can specify additional `archive changes` parameters in the `archive-changes` section of the configuration file. For more details, refer to the [live-migration-with-fall-forward.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/live-migration-with-fall-forward.yaml) template.

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager archive changes --export-dir <EXPORT-DIR> --move-to <DESTINATION-DIR>
```

  {{% /tab %}}

{{< /tabpane >}}

Refer to [archive changes](../../reference/cutover-archive/archive-changes/) for more information.

### Cutover to the target

Cutover is the last phase, where you switch your application over from the source database to the target YugabyteDB database.

Keep monitoring the metrics displayed on `export data from source` and `import data to target` processes. After you notice that the import of events is catching up to the exported events, you are ready to cutover. You can use the "Remaining events" metric displayed in the import data to target process to help you determine the cutover.

<!--When initiating cutover, you can choose the change data capture replication protocol to use using the [--use-yb-grpc-connector](../../reference/cutover-archive/cutover/) flag. By default the flag is true, and migration will use the gRPC replication protocol to export data from target. For YugabyteDB v2024.1.1 or later, you can set the flag to false to choose the PostgreSQL replication protocol. Before importing the schema you need to ensure that there aren't any ALTER TABLE commands that rewrite the table. You can merge the ALTER TABLE commands into their respective CREATE TABLE commands. For more information on CDC in YugabyteDB, refer to [Change data capture](../../../additional-features/change-data-capture/).-->

Perform the following steps as part of the cutover process:

1. Quiesce your source database, that is stop application writes.
1. Perform a cutover after the exported events rate ("Export rate" in the metrics table) drops to 0 using `cutover to target` command (CLI) or using the configuration file.

      <br/>{{< tabpane text=true >}}
{{% tab header="Config file" lang="config" %}}

```sh
yb-voyager initiate cutover to target --config-file <path-to-config-file>
```

{{% /tab %}}

{{% tab header="CLI" lang="cli" %}}

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager initiate cutover to target --export-dir <EXPORT_DIR>
```

{{% /tab %}}

    {{< /tabpane >}}

    {{< note title="CDC options" >}}
The [YugabyteDB Connector](../../../additional-features/change-data-capture/using-logical-replication/yugabytedb-connector/) is the default and GA option (supported in YugabyteDB v2024.2.4+), and is recommended for all deployments. To use the [YugabyteDB gRPC Connector](../../../additional-features/change-data-capture/using-yugabytedb-grpc-replication/debezium-connector-yugabytedb/), explicitly enable it using the `use-yb-grpc-connector` flag or configuration parameter; this connector requires access to the TServer (9100) and Master (7100) ports.
    {{</ note >}}

    Refer to [initiate cutover to target](../../reference/cutover-archive/cutover/#cutover-to-target) for more information.

    As part of the cutover process, the following occurs in the background:

    1. The initiate cutover to target command stops the `export data from source` phase. After this, the `import data to target` phase continues and completes by importing all the exported events into the target YugabyteDB database.

    1. The [export data from target](../../reference/data-migration/export-data/#export-data-from-target) command automatically starts capturing changes from the target YugabyteDB database to the source-replica database.
    Note that the [import data to target](#import-data-to-target) process transforms to an `export data from target` process, so if it gets terminated for any reason, you need to restart process using the `export data from target` command as suggested in the `import data to target` output.

       {{< note title="Event duplication" >}}
The `export data from target` command may result in duplicated events if you restart Voyager, or there is a change in the YugabyteDB database server state. Consequently, the [get data-migration-report](#get-data-migration-report) command may display additional events that have been exported from the target YugabyteDB database, and imported into the source-replica or source database. For such situations, it is recommended to manually verify data in the target and source-replica, or source database to ensure accuracy and consistency.
       {{</ note >}}

1. If there are any NOT VALID constraints on the source, create them after the import data command is completed by using the `finalize-schema-post-data-import` command. If there are [Materialized views](../../../explore/ysql-language-features/advanced-features/views/#materialized-views) in the target YugabyteDB database, you can refresh them by setting the `refresh-mviews` parameter in the `finalize-schema-post-data-import` (configuration file) or use `--refresh-mviews` flag (CLI) with the value true.
    Run the command as follows:

    <br/>{{< tabpane text=true >}}
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

    {{< note title ="Note" >}}
The `import schema --post-snapshot-import` command is deprecated. Use [finalize-schema-post-data-import](../../reference/schema-migration/finalize-schema-post-data-import/) instead.

    {{< /note >}}

1. Verify your migration. After the schema and data import is complete, the automated part of the database migration process is considered complete. You should manually run validation queries on both the source and target YugabyteDB database to ensure that the data is correctly migrated. A sample query to validate the databases can include checking the row count of each table.

    {{< warning title = "Caveat associated with rows reported by get data-migration-report" >}}

Suppose you have the following scenario:

- [import data to target](#import-data-to-target) command fails.
- To resolve this issue, you delete some of the rows from the split files.
- After retrying, the import data to target command completes successfully.

In this scenario, the [get data-migration-report](#get-data-migration-report) command reports an incorrect imported row count because it doesn't take into account the deleted rows.

For more details, refer to the GitHub issue [#360](https://github.com/yugabyte/yb-voyager/issues/360).

    {{< /warning >}}

### Cutover to source-replica (Optional)

During this phase, switch your application over from the target YugabyteDB database to the source-replica database. As this step is optional, perform it _only_ if the target YugabyteDB database is not working as expected.

Keep monitoring the metrics displayed for `export data from target` and `import data to source-replica` processes. After you notice that the import of events to the source-replica database is catching up to the exported events from the target YugabyteDB database, you are ready to cutover. You can use the "Remaining events" metric displayed in the import data to source-replica process to help you determine the cutover.

Perform the following steps as part of the cutover process:

1. Quiesce your target YugabyteDB database, that is stop application writes.
1. Perform a cutover after the exported events rate ("Export rate" in the metrics table) drops to 0 using the following command:

    <br/>{{< tabpane text=true >}}

{{% tab header="Config file" lang="config" %}}

```sh
yb-voyager initiate cutover to source-replica --config-file <path-to-config-file>
```

{{% /tab %}}

{{% tab header="CLI" lang="cli" %}}

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager initiate cutover to source-replica --export-dir <EXPORT_DIR>
```

{{% /tab %}}

    {{< /tabpane >}}

    Refer to [cutover to source-replica](../../reference/cutover-archive/cutover/#cutover-to-source-replica) for more information.

    The `initiate cutover to source-replica` command stops the `export data from target` process, followed by the `import data to source-replica` process after it has imported all the events to the source-replica database.

1. Wait for the cutover process to complete. Monitor the status of the cutover process using the following command:

    <br/>{{< tabpane text=true >}}

{{% tab header="Config file" lang="config" %}}

```sh
yb-voyager cutover status --config-file <path-to-config-file>
```

{{% /tab %}}

{{% tab header="CLI" lang="cli" %}}

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager cutover status --export-dir <EXPORT_DIR>
```

{{% /tab %}}

    {{< /tabpane >}}

    Refer to [cutover status](../../reference/cutover-archive/cutover/#cutover-status) for details about the arguments.

    **Note** that for Oracle migrations, restoring sequences after cutover on the source-replica database is currently unsupported, and you need to restore sequences manually.

1. Set up indexes and triggers to the source-replica database manually. Also, re-enable the foreign key and check constraints.

1. Verify your migration. After the schema and data import is complete, the automated part of the database migration process is considered complete. You should manually run validation queries on both the target and source-replica databases to ensure that the data is correctly migrated. A sample query to validate the databases can include checking the row count of each table.

    {{< warning title = "Caveat associated with rows reported by get data-migration-report" >}}

Suppose you have a scenario where,

- [import data to source-replica](#import-data-to-source-replica) command fails.
- To resolve this issue, you delete some of the rows from the split files.
- After retrying, the import data to target command completes successfully.

In this scenario, [get data-migration-report](#get-data-migration-report) command reports incorrect imported row count; because it doesn't take into account the deleted rows.

For more details, refer to the GitHub issue [#360](https://github.com/yugabyte/yb-voyager/issues/360).

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

To complete the migration, you need to clean up the export directory (export-dir), and Voyager state ( Voyager-related metadata) stored in the target YugabyteDB database and source-replica database.

The `yb-voyager end migration` command performs the cleanup, and backs up the schema, data, migration reports, and log files by providing the backup related arguments.

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

Note that after you end the migration, you will _not_ be able to continue further.

If you want to back up the schema, data, log files, and the migration reports (`analyze-schema` report, `get data-migration-report` output) for future reference, use the `backup-dir` argument (configuration file) or `--backup-dir` flag (CLI), and provide the path of the directory where the backup content needs to be saved (based on what you choose to back up).

Refer to [end migration](../../reference/end-migration/) for more information.

### Delete the ybvoyager user (Optional)

After migration, all the migrated objects (tables, views, and so on) are owned by the `ybvoyager` user. Transfer the ownership of the objects to some other user (for example, `yugabyte`) and then delete the `ybvoyager` user. For example, do the following:

```sql
REASSIGN OWNED BY ybvoyager TO yugabyte;
DROP OWNED BY ybvoyager;
DROP USER ybvoyager;
```

## Limitations

In addition to the Live migration [limitations](../live-migrate/#limitations), the following additional limitations apply to the fall-forward feature:

- For [YugabyteDB gRPC Connector](../../../additional-features/change-data-capture/using-yugabytedb-grpc-replication/debezium-connector-yugabytedb/), fall-forward is unsupported with a YugabyteDB cluster running on YugabyteDB Aeon.
- For YugabyteDB gRPC Connector, [SSL Connectivity](../../reference/yb-voyager-cli/#ssl-connectivity) is partially supported for export or streaming events from YugabyteDB during `export data from target`. Basic SSL and server authentication via root certificate is supported. Client authentication is not supported.
- For YugabyteDB gRPC Connector, the following data types are unsupported when exporting from the target YugabyteDB: BOX, CIRCLE, LINE, LSEG, PATH, PG_LSN, POINT, POLYGON, TSQUERY, TSVECTOR, TXID_SNAPSHOT, GEOMETRY, GEOGRAPHY, RASTER, HSTORE, CITEXT, LTREE, INT4MULTIRANGE, INT8MULTIRANGE, NUMMULTIRANGE, TSMULTIRANGE, TSTZMULTIRANGE, DATEMULTIRANGE, VECTOR, TIMETZ, user-defined types, and array of user-defined types.
- For YugabyteDB Connector (logical replication), the following data types are unsupported when exporting from the target YugabyteDB: BOX, CIRCLE, LINE, LSEG, PATH, PG_LSN, POINT, POLYGON, TSQUERY, TXID_SNAPSHOT, GEOMETRY, GEOGRAPHY, RASTER, INT4MULTIRANGE, INT8MULTIRANGE, NUMMULTIRANGE, TSMULTIRANGE, TSTZMULTIRANGE, DATEMULTIRANGE, VECTOR, TIMETZ, and user-defined range types.
- [Export data from target](../../reference/data-migration/export-data/#export-data-from-target) supports DECIMAL/NUMERIC datatypes for YugabyteDB versions 2.20.1.1 and later.
- [Savepoint](/stable/explore/ysql-language-features/advanced-features/savepoints/) statements within transactions on the target database are not supported. Transactions rolling back to some savepoint may cause data inconsistency between the databases.
- Rows larger than 4MB in the target database can cause consistency issues during migration. Refer to [TA-29060](/stable/releases/techadvisories/ta-29060/) for more details.
- Workloads with [Read Committed isolation level](/stable/architecture/transactions/read-committed/) are not fully supported. It is recommended to use [Repeatable Read or Serializable isolation levels](/stable/architecture/transactions/isolation-levels/) for the duration of the migration.
