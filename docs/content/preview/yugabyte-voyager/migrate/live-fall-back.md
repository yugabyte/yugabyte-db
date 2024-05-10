---
title: Steps to perform live migration of your database with fall-back using YugabyteDB Voyager
headerTitle: Live migration with fall-back
linkTitle: Live migration with fall-back
headcontent: Steps for a live migration with fall-back using YugabyteDB Voyager
description: Steps to ensure a successful live migration with fall-back using YugabyteDB Voyager.
menu:
  preview_yugabyte-voyager:
    identifier: live-fall-back
    parent: migration-types
    weight: 104
techPreview: /preview/releases/versioning/#feature-maturity
type: docs
---

When migrating a database, it's prudent to have a backup strategy in case the new database doesn't work as expected. A fall-back approach involves streaming changes from the YugabyteDB (target) database back to the source database after the cutover operation, enabling you to cutover to the source database at any point.

A fall-back approach allows you to test the system end-to-end. This workflow is especially important in heterogeneous migration scenarios, in which source and target databases are using different engines.

{{< warning title="Important" >}}
This workflow has the potential to alter your source database. Make sure you fully understand the implications of these changes before proceeding.
{{< /warning >}}

## Fall-back workflow

Before starting a live migration, you set up the [source](#prepare-the-source-database) and [target](#prepare-the-target-database) database. During migration, yb-voyager replicates the snapshot data along with new changes exported from the source database to the target YugabyteDB database, as shown in the following illustration:

![fall-back](/images/migrate/live-fall-back-new.png)

At [cutover to target](#cutover-to-the-target), applications stop writing to the source database and start writing to the target YugabyteDB database. After the cutover process is complete, Voyager keeps the source database synchronized with changes from the target YugabyteDB database as shown in the following illustration:

![cutover](/images/migrate/cutover-to-target-fb-new.png)

Finally, if you need to switch back to the source database (because the current YugabyteDB system is not working as expected), you can [cutover to your source](#cutover-to-the-source-optional) database.

![initiate-cutover-to-source](/images/migrate/cutover-to-source-fb-new.png)

The following illustration describes the workflow for live migration using YB Voyager with the fall-back option.

![fall-back-workflow](/images/migrate/fall-back-workflow-new.png)

| Phase | Step | Description |
| :---- | :--- | :---|
| PREPARE | [Install voyager](../../install-yb-voyager/#install-yb-voyager) | yb-voyager supports RHEL, CentOS, Ubuntu, and macOS, as well as airgapped and Docker-based installations. |
| | [Prepare source DB](#prepare-the-source-database) | Create a new database user with READ and WRITE (for fall-back) access to all the resources to be migrated. |
| | [Prepare target DB](#prepare-the-target-database) | Deploy a YugabyteDB database and create a user with necessary privileges. |
| SCHEMA | [Export](#export-schema) | Convert the database schema to PostgreSQL format using the `yb-voyager export schema` command. |
| | [Analyze](#analyze-schema) | Generate a *Schema&nbsp;Analysis&nbsp;Report* using the `yb-voyager analyze-schema` command. The report suggests changes to the PostgreSQL schema to make it appropriate for YugabyteDB. |
| | [Modify](#manually-edit-the-schema) | Using the report recommendations, manually change the exported schema. |
| | [Import](#import-schema) | Import the modified schema to the target YugabyteDB database using the `yb-voyager import schema` command. |
| LIVE MIGRATION | Start | Start the phases: export data first, followed by import data to target and archive changes simultaneously. |
| | [Export data](#export-data-from-source) | The export data command first exports a snapshot and then starts continuously capturing changes from the source.|
| | [Import data](#import-data-to-target) | The import data command first imports the snapshot, and then continuously applies the exported change events on the target. |
| | [Import indexes and triggers to target DB](#import-indexes-and-triggers) | After the snapshot import is complete, import indexes and triggers to the target YugabyteDB database using the `yb-voyager import schema` command with an additional `--post-snapshot-import` flag. |
| | [Archive changes](#archive-changes-optional) | Continuously archive migration changes to limit disk utilization. |
| CUTOVER TO TARGET | [Initiate cutover and prepare for fall-back to target DB](#cutover-to-the-target) | Perform a cutover (stop streaming changes) when the migration process reaches a steady state where you can stop your applications from pointing to your source database, allow all the remaining changes to be applied on the target YugabyteDB database, and then restart your applications pointing to YugabyteDB. |
| | [Wait for cutover to complete](#cutover-to-the-target) | Monitor the wait status using the [cutover status](../../reference/cutover-archive/cutover/#cutover-status) command. |
| | [Verify target DB](#cutover-to-the-target) | Check if the live migration is successful on both the source and the target databases. |
| | [(Manual) Disable triggers and foreign keys on source DB](#cutover-to-the-target) | Run PL/SQL commands to ensure that triggers and foreign key checks are disabled so the data can be imported correctly from the target YugabyteDB database to the source database.  |
| (OPTIONAL) CUTOVER TO SOURCE | [Initiate cutover to source](#cutover-to-the-source-optional) | Perform a cutover (stop streaming changes) when the migration process reaches a steady state where you can stop your applications from pointing to your target YugabyteDB database, allow all the remaining changes to be applied on the source database, and then restart your applications pointing to the source database. |
| | [Wait for cutover to complete](#cutover-to-the-source-optional) | Monitor the wait status using the [cutover status](../../reference/cutover-archive/cutover/#cutover-status) command. |
| | [(Manual)&nbsp;Re-enable triggers and foreign keys on source DB](#cutover-to-the-source-optional) | Run PL/SQL commands to re-enable the triggers and foreign keys on the source database.  |
| | [Verify source DB](#cutover-to-the-source-optional) | Check if the live migration is successful on both the source and the target databases. |
| END | [End migration](#end-migration) | Clean up the migration information stored in export directory and databases (source and target). |

Before proceeding with migration, ensure that you have completed the following steps:

- [Install yb-voyager](../../install-yb-voyager/#install-yb-voyager).
- Review the [guidelines for your migration](../../known-issues/).
- Review [data modeling](../../../develop/learn/data-modeling-ysql) strategies.
- [Prepare the source database](#prepare-the-source-database).
- [Prepare the target database](#prepare-the-target-database).

## Prepare the source database

Create a new database user, and assign the necessary user permissions.

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#oracle" class="nav-link active" id="oracle-tab" data-bs-toggle="tab"
      role="tab" aria-controls="oracle" aria-selected="true">
    <a href="#standalone-oracle" class="nav-link active" id="standalone-oracle-tab" data-bs-toggle="tab" role="tab" aria-controls="oracle" aria-selected="true">
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

  1. Create `ybvoyager_metadata` schema or user, and tables for voyager to use during migration as follows:

     ```sql
     CREATE USER ybvoyager_metadata IDENTIFIED BY "password";
     GRANT CONNECT, RESOURCE TO ybvoyager_metadata;
     ALTER USER ybvoyager_metadata QUOTA UNLIMITED ON USERS;

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

1. Create a writer role for the source schema for Voyager to be able to write the changes from the target YugabyteDB database to the source database (in case of a fall-back):

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

1. Assign the writer role to the source database user as follows:

   ```sql
   GRANT <SCHEMA_NAME>_writer_role TO c##ybvoyager;
   ```

  {{% /tab %}}

  {{% tab header="RDS Oracle" %}}

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

  1. Create `ybvoyager_metadata` schema or user, and tables for voyager to use during migration as follows:

     ```sql
     CREATE USER ybvoyager_metadata IDENTIFIED BY "password";
     GRANT CONNECT, RESOURCE TO ybvoyager_metadata;
     ALTER USER ybvoyager_metadata QUOTA UNLIMITED ON USERS;

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

1. Create a writer role for the source schema for Voyager to be able to write the changes from the target YugabyteDB database to the source database (in case of a fall-back):

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

1. Assign the writer role to the source database user as follows:

   ```sql
   GRANT <SCHEMA_NAME>_writer_role TO ybvoyager;
   ```

  {{% /tab %}}

{{< /tabpane >}}

  </div>
  <div id="pg" class="tab-pane fade" role="tabpanel" aria-labelledby="pg-tab">

{{< tabpane text=true >}}

  {{% tab header="Standalone PostgreSQL" %}}

1. yb_voyager requires `wal_level` to be logical. You can check this using following the steps:

    1. Run the command `SHOW wal_level` on the database to check the value.

    1. If the value is anything other than logical, run the command `SHOW config_file` to know the path of your configuration file.

    1. Modify the configuration file by uncommenting the parameter `wal_level` and set the value to logical.

    1. Restart PostgreSQL.

1. Check that the replica identity is FULL (`f`)  for all tables on the database.

    1. Check the replica identity using the following query:

        ```sql
        SELECT n.nspname, relname, relreplident
        FROM pg_class c JOIN pg_namespace n on c.relnamespace = n.oid
        WHERE n.nspname in (<SCHEMA_LIST>) and relkind = 'r';
        --- SCHEMA_LIST used is a comma-separated list of schemas, for example, SCHEMA_LIST 'abc','public', 'xyz'.
        ```

    1. Change the replica identity of all tables if the tables have an identity other than FULL (`f`), using the following query:

        ```sql
        DO $$
        DECLARE
          r Record;
        BEGIN
          FOR r IN (SELECT table_schema, '"' || table_name || '"' as t_name  FROM information_schema.tables WHERE table_schema IN (<SCHEMA_LIST>) AND table_type = 'BASE TABLE')
          LOOP
            EXECUTE 'ALTER TABLE ' || r.table_schema || '.' || r.t_name || ' REPLICA IDENTITY FULL';
          END LOOP;
        END $$;
        --- SCHEMA_LIST used is a comma-separated list of schemas, for example, SCHEMA_LIST 'abc','public', 'xyz'.
        ```

1. Create user `ybvoyager` for the migration using the following command:

    ```sql
    CREATE USER ybvoyager PASSWORD 'password' REPLICATION;
    ```

1. Switch to the database that you want to migrate as follows:

   ```sql
   \c <database_name>
   ```

1. Grant the `USAGE` permission to the `ybvoyager` user on all schemas of the database as follows:

   ```sql
   SELECT 'GRANT USAGE ON SCHEMA ' || schema_name || ' TO ybvoyager;' FROM information_schema.schemata; \\gexec
   ```

   The preceding `SELECT` statement generates a list of `GRANT USAGE` statements which are then executed by `psql` because of the `\\gexec` switch. The `\\gexec` switch works for PostgreSQL v9.6 and later. For earlier versions, you'll have to manually execute the `GRANT USAGE ON SCHEMA schema_name TO ybvoyager` statement, for each schema in the source PostgreSQL database.

1. Grant `SELECT` permission on all the tables and sequences as follows:

   ```sql
   SELECT 'GRANT SELECT ON ALL TABLES IN SCHEMA ' || schema_name || ' TO ybvoyager;' FROM information_schema.schemata; \\gexec

   SELECT 'GRANT SELECT ON ALL SEQUENCES IN SCHEMA ' || schema_name || ' TO ybvoyager;' FROM information_schema.schemata; \\gexec
   ```

1. Create a replication group as follows:

    ```sql
    CREATE ROLE replication_group;
    ```

1. Add the original owner of the table to the group as follows:

    ```sql
    GRANT replication_group TO <original_owner>;
    ```

1. Add the user `ybvoyager` to the replication group as follows:

    ```sql
    GRANT replication_group TO ybvoyager;
    ```

1. Transfer ownership of the tables to the role `replication_group` as follows:

    ```sql
    DO $$
    DECLARE
      r Record;
    BEGIN
      FOR r IN (SELECT table_schema, '"' || table_name || '"' as t_name FROM information_schema.tables WHERE table_schema IN (<SCHEMA_LIST>))
      LOOP
        EXECUTE 'ALTER TABLE ' || r.table_schema || '.' || r.t_name || ' OWNER TO replication_group';
      END LOOP;
    END $$;
    --- SCHEMA_LIST used is a comma-separated list of schemas, for example, SCHEMA_LIST 'abc','public', 'xyz'.
    ```

1. Grant `CREATE` privilege on the source database to `ybvoyager` as follows:

    ```sql
    GRANT CREATE ON DATABASE <database_name> TO ybvoyager; --required to create a publication.
    ```

    The `ybvoyager` user can now be used for migration.

1. Provide write privileges on all tables in the schemas to the Voyager user to be able to write the changes from target YugabyteDB to the source database (in case of a fall-back).

    ```sql
    SELECT 'GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA ' || schema_name || ' TO ybvoyager;' FROM information_schema.schemata where schema_name IN (<SCHEMA_LIST>) ; \\gexec
    --- SCHEMA_LIST used is a comma-separated list of schemas, for example, SCHEMA_LIST 'abc','public', 'xyz'.
    ```

  {{% /tab %}}

  {{% tab header="RDS PostgreSQL" %}}

1. yb_voyager requires `wal_level` to be logical. This is controlled by a database parameter `rds.logical_replication` which needs to be set to 1. You can check this using following the steps:

    1. Run the command `SHOW rds.logical_replication` on the database to check whether the parameter is set.

    1. If the parameter is not set, you can change the parameter value to 1 from the RDS console of the database; navigate to **Configuration** > **Parameter group** > `rds.logical_replication`.

    1. If the `rds.logical_replication` errors out (after the change), create a new parameter group with the value as 1, and assign it to the database instance from the **Modify** option on the RDS console.

    1. Restart RDS.

1. Check that the replica identity is FULL (`f`) for all tables on the database.

    1. Check the Replica identity for all the tables on the database as follows:

        ```sql
        SELECT n.nspname, relname, relreplident
        FROM pg_claspg_class c JOIN pg_namespace n on c.relnamespace = n.oids
        WHERE n.nspname in (<SCHEMA_LIST>) and relkind = 'r';
        --- SCHEMA_LIST used is a comma-separated list of schemas, for example, SCHEMA_LIST 'abc','public', 'xyz'.
        ```

    1. Change the replica identity of all tables if the tables have an identity other than FULL (`f`), using the following query:

        ```sql
        DO $$
        DECLARE
          r Record;
        BEGIN
          FOR r IN (SELECT table_schema, '"' || table_name || '"' as t_name  FROM information_schema.tables WHERE table_schema IN (<SCHEMA_LIST>) AND table_type = 'BASE TABLE')
          LOOP
            EXECUTE 'ALTER TABLE ' || r.table_schema || '.' || r.t_name || ' REPLICA IDENTITY FULL';
          END LOOP;
        END $$;
        --- SCHEMA_LIST used is a comma-separated list of schemas, for example, SCHEMA_LIST 'abc','public', 'xyz'.
        ```

1. Create user `ybvoyager` for the migration using the following command:

    ```sql
    CREATE USER ybvoyager PASSWORD 'password';
    GRANT rds_replication to ybvoyager;
    ```

1. Switch to the database that you want to migrate as follows:

   ```sql
   \\c <database_name>
   ```

1. Grant the `USAGE` permission to the `ybvoyager` user on all schemas of the database as follows:

   ```sql
   SELECT 'GRANT USAGE ON SCHEMA ' || schema_name || ' TO ybvoyager;' FROM information_schema.schemata; \\gexec
   ```

   The preceding `SELECT` statement generates a list of `GRANT USAGE` statements which are then executed by `psql` because of the `\\gexec` switch. The `\\gexec` switch works for PostgreSQL v9.6 and later. For older versions, you'll have to manually execute the `GRANT USAGE ON SCHEMA schema_name TO ybvoyager` statement, for each schema in the source PostgreSQL database.

1. Grant `SELECT` permission on all the tables and sequences as follows:

   ```sql
   SELECT 'GRANT SELECT ON ALL TABLES IN SCHEMA ' || schema_name || ' TO ybvoyager;' FROM information_schema.schemata; \\gexec

   SELECT 'GRANT SELECT ON ALL SEQUENCES IN SCHEMA ' || schema_name || ' TO ybvoyager;' FROM information_schema.schemata; \\gexec
   ```

1. Create a replication group as follows:

    ```sql
    CREATE ROLE replication_group;
    ```

1. Add the original owner of the table to the group as follows:

    ```sql
    GRANT replication_group TO <original_owner>;
    ```

1. Add the user `ybvoyager` to the replication group as follows:

    ```sql
    GRANT replication_group TO ybvoyager;
    ```

1. Transfer ownership of the tables to the role <replication_group> as follows:

    ```sql
    DO $$
    DECLARE
      r Record;
    BEGIN
      FOR r IN (SELECT table_schema, '"' || table_name || '"' as t_name FROM information_schema.tables WHERE table_schema IN (<SCHEMA_LIST>))
      LOOP
        EXECUTE 'ALTER TABLE ' || r.table_schema || '.' || r.t_name || ' OWNER TO replication_group';
      END LOOP;
    END $$;
    --- SCHEMA_LIST used is a comma-separated list of schemas, for example, SCHEMA_LIST 'abc','public', 'xyz'.
    ```

1. Grant `CREATE` privilege on the source database to `ybvoyager` as follows:

    ```sql
    GRANT CREATE ON DATABASE <database_name> TO ybvoyager; --required to create a publication.
    ```

    The `ybvoyager` user can now be used for migration.

1. Provide write privileges on all tables in the schemas to the Voyager user to be able to write the changes from target YugabyteDB to the source database (in case of a fall-back):

    ```sql
    SELECT 'GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA ' || schema_name || ' TO ybvoyager;' FROM information_schema.schemata where schema_name IN (<SCHEMA_LIST>) ; \\gexec
    --- SCHEMA_LIST used is a comma-separated list of schemas, for example, SCHEMA_LIST 'abc','public', 'xyz'.
    ```

  {{% /tab %}}

{{< /tabpane >}}

</div>

If you want yb-voyager to connect to the source database over SSL, refer to [SSL Connectivity](../../reference/yb-voyager-cli/#ssl-connectivity).

{{< note title="Connecting to Oracle instances" >}}
You can use only one of the following arguments to connect to your Oracle instance:

- --source-db-schema (Schema name of the source database.)
- --oracle-db-sid (Oracle System Identifier you can use while exporting data from Oracle instances.)
- --oracle-tns-alias (TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server.)
{{< /note >}}

## Prepare the target database

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

If you don't provide the target YugabyteDB database name during import, yb-voyager assumes the target YugabyteDB database name is `yugabyte`. To specify the target YugabyteDB database name during import, use the `--target-db-name` argument with the `yb-voyager import` commands.

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

yb-voyager keeps all of its migration state, including exported schema and data, in a local directory called the _export directory_.

Before starting migration, you should create the export directory on a file system that has enough space to keep the entire source database. Next, you should provide the path of the export directory as a mandatory argument (`--export-dir`) to each invocation of the yb-voyager command in an environment variable.

```sh
mkdir $HOME/export-dir
export EXPORT_DIR=$HOME/export-dir
```

The export directory has the following sub-directories and files:

- `reports` directory contains the generated _Schema Analysis Report_.
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

{{< note title="Usage for source_db_schema" >}}

The `source_db_schema` argument specifies the schema of the source database.

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
        --source-db-schema <SOURCE_DB_SCHEMA>

```

Refer to [export schema](../../reference/schema-migration/export-schema/) for details about the arguments.

#### Analyze schema

The schema exported in the previous step may not yet be suitable for importing into YugabyteDB. Even though YugabyteDB is PostgreSQL compatible, given its distributed nature, you may need to make minor manual changes to the schema.

The `yb-voyager analyze-schema` command analyses the PostgreSQL schema dumped in the [export schema](#export-schema) step, and prepares a report that lists the DDL statements which need manual changes. An example invocation of the command with required arguments is as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager analyze-schema --export-dir <EXPORT_DIR> --output-format <FORMAT>
```

The preceding command generates a report file under the `EXPORT_DIR/reports/` directory.

Refer to [analyze schema](../../reference/schema-migration/analyze-schema/) for details about the arguments.

#### Manually edit the schema

Fix all the issues listed in the generated schema analysis report by manually editing the SQL DDL files from the `EXPORT_DIR/schema/*`.

After making the manual changes, re-run the `yb-voyager analyze-schema` command. This generates a fresh report using your changes. Repeat these steps until the generated report contains no issues.

To learn more about modelling strategies using YugabyteDB, refer to [Data modeling](../../reference/data-modeling/).

{{< note title="Manual schema changes" >}}

- Include the primary key definition in the `CREATE TABLE` statement. Primary Key cannot be added to a partitioned table using the `ALTER TABLE` statement.

{{< /note >}}

### Import schema

Import the schema using the `yb-voyager import schema` command.

{{< note title="Usage for target_db_schema" >}}

`yb-voyager` imports the source database into the `public` schema of the target YugabyteDB database. By specifying `--target-db-schema` argument during import, you can instruct `yb-voyager` to create a non-public schema and use it for the schema/data import.

{{< /note >}}

An example invocation of the command with required arguments is as follows:

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

yb-voyager applies the DDL SQL files located in the `$EXPORT_DIR/schema` directory to the target YugabyteDB database. If yb-voyager terminates before it imports the entire schema, you can rerun it by adding the `--ignore-exist` option.

{{< note title="Importing indexes and triggers" >}}

Because the presence of indexes and triggers can slow down the rate at which data is imported, by default `import schema` does not import indexes (except UNIQUE indexes to avoid any issues during import of schema because of foreign key dependencies on the index) and triggers. You should complete the data import without creating indexes and triggers. Only after data import is complete, create indexes and triggers using the `import schema` command with an additional `--post-snapshot-import` flag.

{{< /note >}}

### Export data from source

Begin exporting data from the source database into the `EXPORT_DIR/data` directory using the yb-voyager export data from source command with required arguments as follows:

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

The export data from source command first ensures that it exports a snapshot of the data already present on the source database. Next, you start a streaming phase (CDC phase) where you begin capturing new changes made to the data on the source after the migration has started. Some important metrics such as the number of events, export rate, and so on, is displayed during the CDC phase similar to the following:

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

Note that the CDC phase will start only after a snapshot of the entire table-set is completed.
Additionally, the CDC phase is restartable. So, if yb-voyager terminates when data export is in progress, it resumes from its current state after the CDC phase is restarted.

#### Caveats

- Some data types are unsupported. For a detailed list, refer to [datatype mappings](../../reference/datatype-mapping-oracle/).
- For Oracle where sequences are not attached to a column, resume value generation is unsupported.
- `--parallel-jobs` argument (specifies the number of tables to be exported in parallel from the source database at a time) has no effect on live migration.

Refer to [export data](../../reference/data-migration/export-data/#export-data) for details about the arguments of an export operation.

The options passed to the command are similar to the [`yb-voyager export schema`](#export-schema) command. To export only a subset of the tables, pass a comma-separated list of table names in the `--table-list` argument.

#### get data-migration-report

Run the `yb-voyager get data-migration-report --export-dir <EXPORT_DIR>` command to get a consolidated report of the overall progress of data migration concerning all the databases involved (source and target).

Refer to [get data-migration-report](../../reference/data-migration/export-data/#get-data-migration-report) for details about the arguments.

### Import data to target

After you have successfully imported the schema in the target YugabyteDB database, you can start importing the data using the yb-voyager import data to target command as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager import data to target --export-dir <EXPORT_DIR> \
        --target-db-host <TARGET_DB_HOST> \
        --target-db-user <TARGET_DB_USER> \
        --target-db-password <TARGET_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name <TARGET_DB_NAME> \
        --target-db-schema <TARGET_DB_SCHEMA> \ # Oracle only.
        --parallel-jobs <NUMBER_OF_JOBS>
```

Refer to [import data](../../reference/data-migration/import-data/#import-data) for details about the arguments.

For the snapshot exported, yb-voyager splits the data dump files (from the $EXPORT_DIR/data directory) into smaller batches. yb-voyager concurrently ingests the batches such that all nodes of the target YugabyteDB database cluster are used. After the snapshot is imported, a similar approach is employed for the CDC phase, where concurrent batches of change events are applied on the target YugabyteDB database cluster.

Some important metrics such as the number of events, ingestion rate, and so on, is displayed during the CDC phase similar to the following:

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

#### get data-migration-report

Run the following command with required arguments to get a consolidated report of the overall progress of data migration concerning all the databases involved (source and target).

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager get data-migration-report --export-dir <EXPORT_DIR> \
        --target-db-password <TARGET_DB_PASSWORD> \
        --source-db-password <SOURCE_DB_PASSWORD>
```

Refer to [get data-migration-report](../../reference/data-migration/import-data/#get-data-migration-report) for details about the arguments.

#### Import indexes and triggers

Import indexes and triggers on the target YugabyteDB database after the `import data to target` has completed the following tasks:

- The exported snapshot has been completely imported on the target.
- All the events accumulated on local disk by [export data from source](#export-data-from-source) during the snapshot import phase and [import data to target](#import-data-to-target) have caught up in the CDC phase (you can monitor the timeline based on `Estimated Time to catch up` metric).

After the preceding steps are completed, you can start importing indexes and triggers in parallel with the `import data to target` command using the `import schema` command with an additional `--post-snapshot-import` flag as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager import schema --export-dir <EXPORT_DIR> \
        --target-db-host <TARGET_DB_HOST> \
        --target-db-user <TARGET_DB_USER> \
        --target-db-password <TARGET_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name <TARGET_DB_NAME> \
        --target-db-schema <TARGET_DB_SCHEMA> \
        --post-snapshot-import true
```

If any of the CREATE INDEX DDLs fail in the preceding command, retry the command with the argument `--ignore-exist` to ignore already created indexes and create new ones instead.

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager import schema --export-dir <EXPORT_DIR> \
        --target-db-host <TARGET_DB_HOST> \
        --target-db-user <TARGET_DB_USER> \
        --target-db-password <TARGET_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name <TARGET_DB_NAME> \
        --target-db-schema <TARGET_DB_SCHEMA> \
        --post-snapshot-import true \
        --ignore-exist true
```

Refer to [import schema](../../reference/schema-migration/import-schema/) for details about the arguments.

### Archive changes (Optional)

As the migration continuously exports changes on the source database to the `EXPORT-DIR`, the disk utilization continues to grow indefinitely over time. To limit usage of all the disk space, optionally, you can use the `archive changes` command as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager archive changes --export-dir <EXPORT-DIR> --move-to <DESTINATION-DIR>
```

Refer to [archive changes](../../reference/cutover-archive/archive-changes/) for details about the arguments.

### Cutover to the target

During cutover, you switch your application over from the source database to the target YugabyteDB database.

Keep monitoring the metrics displayed for export data from source and import data to target processes. After you notice that the import of events is catching up to the exported events, you are ready to perform a cutover. You can use the "Remaining events" metric displayed in the import data to target process to help you determine the cutover.

Perform the following steps as part of the cutover process:

1. Quiesce your source database, that is stop application writes.
1. Perform a cutover after the exported events rate ("Export rate" in the metrics table) drops to 0 using the following command:

    ```sh
    # Replace the argument values with those applicable for your migration.
    yb-voyager initiate cutover to target --export-dir <EXPORT_DIR> --prepare-for-fall-back true
    ```

    Refer to [initiate cutover to target](../../reference/cutover-archive/cutover/#cutover-initiate) for details about the arguments.

    As part of the cutover process, the following occurs in the background:

    1. The initiate cutover to target command stops the export data from source process, followed by the import data to target process after it has imported all the events to the target YugabyteDB database.

    1. The [export data from target](../../reference/data-migration/export-data/#export-data-from-target) command automatically starts capturing changes from the target YugabyteDB database.
    Note that the [import data to target](#import-data-to-target) process transforms to an `export data from target` process, so if it gets terminated for any reason, you need to restart the process using the `export data from target` command as suggested in the `import data to target` output.

       {{<note title="Event duplication">}}
The `export data from target` command may result in duplicated events if you restart Voyager, or there is a change in the YugabyteDB database server state. Consequently, the [get data-migration-report](#get-data-migration-report) command may display additional events that have been exported from the target YugabyteDB database, and imported into the source database. For such situations, it is recommended to manually verify data in the source or target database to ensure accuracy and consistency.
        {{</note>}}

    1. The [import data to source](../../reference/data-migration/import-data/#import-data-to-source) command automatically starts applying changes (captured from the target YugabyteDB) back to the source database.
    Note that the [export data from source](#export-data-from-source) process transforms to a `import data to source` process, so if it gets terminated for any reason, you need to restart the process using `import data to source` command as suggested in the `export data from source` output.

1. Wait for the cutover process to complete. Monitor the status of the cutover process using the following command:

    ```sh
    # Replace the argument values with those applicable for your migration.
    yb-voyager cutover status --export-dir <EXPORT_DIR>
    ```

    Refer to [cutover status](../../reference/cutover-archive/cutover/#cutover-status) for details about the arguments.

1. If there are [Materialized views](../../../explore/ysql-language-features/advanced-features/views/#materialized-views) in the migration, refresh them manually after cutover.

1. Verify your migration. After the schema and data import is complete, the automated part of the database migration process is considered complete. You should manually run validation queries on both the source and target YugabyteDB database to ensure that the data is correctly migrated. A sample query to validate the databases can include checking the row count of each table.

    {{< warning title = "Caveat associated with rows reported by get data-migration-report" >}}

Suppose you have the following scenario:

- [import data to target](#import-data-to-target) or [import data file](../bulk-data-load/#import-data-files-from-the-local-disk) command fails.
- To resolve this issue, you delete some of the rows from the split files.
- After retrying, the import data to target command completes successfully.

In this scenario, the [get data-migration-report](#get-data-migration-report) command reports an incorrect imported row count because it doesn't take into account the deleted rows.

For more details, refer to the GitHub issue [#360](https://github.com/yugabyte/yb-voyager/issues/360).

    {{< /warning >}}

1. Disable triggers and foreign-key constraints on the source database to ensure that changes from the target YugabyteDB database can be imported correctly to the source database using the following PL/SQL commands on the source schema as a privileged user:<br>

{{< tabpane text=true >}}

  {{% tab header="Oracle" %}}

Use the following PL/SQL commands to disable triggers, and disable referential constraints on the source:

  ```sql
    --disable triggers
  BEGIN
      FOR R IN (SELECT owner, object_name FROM all_objects WHERE owner=UPPER('<SCHEMA_NAME>') and object_type ='TABLE' MINUS SELECT owner, table_name from all_nested_tables where owner = UPPER('<SCHEMA_NAME>'))
      LOOP
        EXECUTE IMMEDIATE 'ALTER TABLE '||R.owner||'."'||R.object_name||'" DISABLE ALL TRIGGERS';
      END LOOP;
  END;
    /

    --disable referential constraints
  BEGIN
      FOR c IN (SELECT table_name, constraint_name
        FROM user_constraints
        WHERE constraint_type IN ('R') AND OWNER = '<SCHEMA_NAME>')
      LOOP
        EXECUTE IMMEDIATE 'ALTER TABLE ' || c.table_name || ' DISABLE CONSTRAINT ' || c.constraint_name;
      END LOOP;
  END;
    /
  ```

  {{% /tab %}}

  {{% tab header="PostgreSQL" %}}

  Use the following PL/SQL commands to disable triggers, and drop foreign-key constraints on the source:

  ```sql
  --disable triggers
  DO $$
  DECLARE
      r RECORD;
  BEGIN
      FOR r IN
          SELECT table_schema, '"' || table_name || '"' AS t_name
          FROM information_schema.tables
          WHERE table_type = 'BASE TABLE'
          AND table_schema IN (<SCHEMA_LIST>)
      LOOP
          EXECUTE 'ALTER TABLE ' || r.table_schema || '.' || r.t_name || ' DISABLE TRIGGER ALL';
      END LOOP;
  END $$;
  --- SCHEMA_LIST used is a comma-separated list of schemas, for example, SCHEMA_LIST 'abc','public', 'xyz'.

  --drop referential constraints
  DO $$
  DECLARE
      fk RECORD;
  BEGIN
      FOR fk IN
          SELECT conname, conrelid::regclass AS table_name
          FROM pg_constraint
          JOIN pg_class ON conrelid = pg_class.oid
          JOIN pg_namespace ON pg_namespace.oid = pg_class.relnamespace
          WHERE contype = 'f'
          AND pg_namespace.nspname IN (<SCHEMA_LIST>)
      LOOP
          EXECUTE 'ALTER TABLE ' || fk.table_name || ' DROP CONSTRAINT ' || fk.conname;
      END LOOP;
  END $$;
  --- SCHEMA_LIST used is a comma-separated list of schemas, for example, SCHEMA_LIST 'abc','public', 'xyz'.

  ```

  {{% /tab %}}

{{< /tabpane >}}

### Cutover to the source (Optional)

During this phase, switch your application over from the target YugabyteDB database back to the source database.

Perform this step only if the target YugabyteDB database is not working as expected.

Keep monitoring the metrics displayed for `export data from target` and `import data to source` processes. After you notice that the import of events to the source database is catching up to the exported events from the target YugabyteDB database, you are ready to cutover. You can use the "Remaining events" metric displayed in the `import data to source` process to help you determine the cutover.
Perform the following steps as part of the cutover process:

1. Quiesce your target YugabyteDB database, that is stop application writes.

1. Perform a cutover after the exported events rate ("Export rate" in the metrics table) drops to 0 using the following command:

    ```sh
    # Replace the argument values with those applicable for your migration.
    yb-voyager initiate cutover to source --export-dir <EXPORT_DIR>
    ```

    Refer to [cutover to source](../../reference/cutover-archive/cutover/#cutover-to-source) for details about the arguments.

    The `initiate cutover to source` command stops the `export data from target` process, followed by the `import data to source` process after it has imported all the events to the source database.

1. Wait for the cutover process to complete. Monitor the status of the cutover process using the following command:

    ```sh
    # Replace the argument values with those applicable for your migration.
    yb-voyager cutover status --export-dir <EXPORT_DIR>
    ```

    Refer to [cutover status](../../reference/cutover-archive/cutover/#cutover-status) for details about the arguments.

    **Note** that for Oracle migrations, restoring sequences after cutover on the source-replica database is currently unsupported, and you need to restore sequences manually.

1. Re-enable triggers and foreign-key constraints on the source database using the following PL/SQL commands on the source schema as a privileged user:

{{< tabpane text=true >}}

  {{% tab header="Oracle" %}}

  ```sql
  --enable triggers
  BEGIN
      FOR R IN (SELECT owner, object_name FROM all_objects WHERE owner=UPPER('<SCHEMA_NAME>') and object_type ='TABLE' MINUS SELECT owner, table_name from all_nested_tables where owner = UPPER('<SCHEMA_NAME>'))
      LOOP
         EXECUTE IMMEDIATE 'ALTER TABLE '||R.owner||'."'||R.object_name||'" ENABLE ALL TRIGGERS';
      END LOOP;
  END;
    /

    --enable referential constraints

  BEGIN
      FOR c IN (SELECT table_name, constraint_name
              FROM user_constraints
              WHERE constraint_type IN ('R') AND OWNER = '<SCHEMA_NAME>' )
      LOOP
         EXECUTE IMMEDIATE 'ALTER TABLE ' || c.table_name || ' ENABLE CONSTRAINT ' || c.constraint_name;
      END LOOP;
  END;
    /
  ```

  {{% /tab %}}

  {{% tab header="PostgreSQL" %}}

  Use the following PL/SQL to enable the triggers and create foreign key constraints back before using the source again.

  ```sql
  --disable triggers
  DO $$
  DECLARE
      r RECORD;
  BEGIN
      FOR r IN
          SELECT table_schema, '"' || table_name || '"' AS t_name
          FROM information_schema.tables
          WHERE table_type = 'BASE TABLE'
          AND table_schema IN (<SCHEMA_LIST>)
      LOOP
          EXECUTE 'ALTER TABLE ' || r.table_schema || '.' || r.t_name || ' ENABLE TRIGGER ALL';
      END LOOP;
  END $$;
  --- SCHEMA_LIST used is a comma-separated list of schemas, for example, SCHEMA_LIST 'abc','public', 'xyz'.

  --create referential constraints
  --you can use schema dump from source which is use to import schema on target YugabyteDB (with the modifications if made in schema migration phase), one copy of the pure form of that dump is stored in `$EXPORT_DIR/temp/schema.sql`.
  ```

  {{% /tab %}}

{{< /tabpane >}}

1. Verify your migration. After the schema and data import is complete, the automated part of the database migration process is considered complete. You should manually run validation queries on both the source and target databases to ensure that the data is correctly migrated. A sample query to validate the databases can include checking the row count of each table.

### End migration

To complete the migration, you need to clean up the export directory (export-dir), and Voyager state ( Voyager-related metadata) stored in the target YugabyteDB database and source database.

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

Note that after you end the migration, you will _not_ be able to continue further. If you want to back up the schema, data, log files, and the migration reports (`analyze-schema` report and `get data-migration-report` output) for future reference, the command provides an additional argument `--backup-dir`, using which you can pass the path of the directory where the backup content needs to be saved (based on what you choose to back up).

Refer to [end migration](../../reference/end-migration/) for more details on the arguments.

## Limitations

In addition to the Live migration [limitations](../live-migrate/#limitations), the following additional limitations apply to the fall-back feature:

- Fall-back is unsupported with a YugabyteDB cluster running on YugabyteDB Managed.
- SSL Connectivity is unsupported for export or streaming events from YugabyteDB during `export data from target`.
- In the fall-back phase, you need to manually disable (and subsequently re-enable if required) constraints/indexes/triggers on the source database.
- [Export data from target](../../reference/data-migration/export-data/#export-data-from-target) supports DECIMAL/NUMERIC datatypes for YugabyteDB versions 2.20.1.1 and later.
