<!--
+++
private=true
+++
-->

1. Ensure that your database log_mode is `archivelog` as follows:

    ```sql
    SELECT LOG_MODE FROM V$DATABASE;
    LOG_MODE
    ------------
    ARCHIVELOG

    If not enabled,
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
    ALTER DATABASE ADD SUPPLEMENTAL LOG DATA (PRIMARY KEY) COLUMNS;
    ```
