<!--
+++
private=true
+++
-->

Create a role and a database user, and provide the user with READ access to all the resources which need to be migrated.

1. Create a role that has the privileges as listed in the following table:

   | Permission | Object type in the source schema |
   | :--------- | :---------------------------------- |
   | `SELECT` | VIEW, SEQUENCE, TABLE PARTITION, TABLE, SYNONYM, MATERIALIZED VIEW |
   | `EXECUTE` | TYPE |

   Change the `<SCHEMA_NAME>` appropriately in the following snippets, and run the following steps as a privileged user.

   ```sql
   CREATE ROLE <SCHEMA_NAME>_reader_role;

   BEGIN
       FOR R IN (SELECT owner, object_name FROM all_objects WHERE owner=UPPER('<SCHEMA_NAME>') and object_type in ('VIEW','SEQUENCE','TABLE PARTITION','SYNONYM','MATERIALIZED VIEW'))
       LOOP
          EXECUTE IMMEDIATE 'grant select on '||R.owner||'."'||R.object_name||'" to <SCHEMA_NAME>_reader_role';
       END LOOP;
   END;
   /

   BEGIN
       FOR R IN (SELECT owner, object_name FROM all_objects WHERE owner=UPPER('<SCHEMA_NAME>') and object_type ='TABLE' MINUS SELECT owner, table_name from all_nested_tables where owner = UPPER('<SCHEMA_NAME>'))
       LOOP
          EXECUTE IMMEDIATE 'grant select on '||R.owner||'."'||R.object_name||'" to  <SCHEMA_NAME>_reader_role';
       END LOOP;
   END;
   /

   BEGIN
       FOR R IN (SELECT owner, object_name FROM all_objects WHERE owner=UPPER('<SCHEMA_NAME>') and object_type = 'TYPE')
       LOOP
          EXECUTE IMMEDIATE 'grant execute on '||R.owner||'."'||R.object_name||'" to <SCHEMA_NAME>_reader_role';
       END LOOP;
   END;
   /

   GRANT SELECT_CATALOG_ROLE TO <SCHEMA_NAME>_reader_role;
   GRANT SELECT ANY DICTIONARY TO <SCHEMA_NAME>_reader_role;
   GRANT SELECT ON SYS.ARGUMENT$ TO <SCHEMA_NAME>_reader_role;

   ```

1. Create a user `ybvoyager` and grant `CONNECT` and `<SCHEMA_NAME>_reader_role` to the user:

   ```sql
   CREATE USER ybvoyager IDENTIFIED BY password;
   GRANT CONNECT TO ybvoyager;
   GRANT <SCHEMA_NAME>_reader_role TO ybvoyager;
   ```

   If you're using [accelerated data export](#accelerate-data-export-for-mysql-and-oracle), run the additional grant as follows:

   ```sql
   GRANT FLASHBACK ANY TABLE TO ybvoyager;
   ```

1. If you're using [accelerated data export](#accelerate-data-export-for-mysql-and-oracle), the `log_mode` should be archivelog.

    1. Check the value for `log_mode` using the following command for Oracle/Oracle RDS:

        ```sql
        SELECT LOG_MODE FROM V$DATABASE;
        ```

    1. If the `log_mode` value is `NOARCHIVELOG`, run the following commands:

       **For Oracle**

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

       **For Oracle RDS**

       ```sql
       exec rdsadmin.rdsadmin_util.set_configuration('archivelog retention hours',24);
       ```

    1. Verify using `archive log list`.

If you want yb-voyager to connect to the source database over SSL, refer to [SSL Connectivity](../../reference/yb-voyager-cli/#ssl-connectivity). Note that you can use only one of the following arguments to connect to your Oracle instance:

- --source-db-schema (Schema name of the source database.)
- --oracle-db-sid (Oracle System Identifier you can use while exporting data from Oracle instances.)
- --oracle-tns-alias (TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server.)
