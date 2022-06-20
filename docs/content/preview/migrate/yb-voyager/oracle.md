
Create a role and a database user, and provide the user with READ access to all the resources which need to be migrated.

1. Create a role that has the privileges as listed in the following table:

   | Permission | Object type in the source schema |
   | :--------- | :---------------------------------- |
   | `SELECT` | VIEW, SEQUENCE, TABLE PARTITION, TABLE, SYNONYM, MATERIALIZED VIEW |
   | `EXECUTE` | PROCEDURE, FUNCTION, PACKAGE, PACKAGE BODY, TYPE |

   Change the `<SCHEMA_NAME>` appropriately in the following snippets, and run the following steps as a privileged user.

   ```sql
   CREATE ROLE schema_ro_role;
     BEGIN
       FOR R IN (SELECT owner, object_name FROM all_objects WHERE owner='<SCHEMA_NAME>' and object_type in ('VIEW','SEQUENCE','TABLE PARTITION','TABLE','SYNONYM','MATERIALIZED VIEW')) LOOP
           EXECUTE IMMEDIATE 'grant select on '||R.owner||'."'||R.object_name||'" to schema_ro_role';
     END LOOP;
   END;

     BEGIN
       FOR R IN (SELECT owner, object_name FROM all_objects WHERE owner='<SCHEMA_NAME>' and object_type in ('PROCEDURE','FUNCTION','PACKAGE','PACKAGE BODY', 'TYPE')) LOOP
           EXECUTE IMMEDIATE 'grant execute on '||R.owner||'."'||R.object_name||'" to schema_ro_role';
     END LOOP;
   END;
   ```

1. Create a user `ybvoyager` and grant `CONNECT` and `schema_ro_role` to the user:

   ```sql
   CREATE USER ybvoyager IDENTIFIED BY password;
   GRANT CONNECT TO ybvoyager;
   GRANT schema_ro_role TO ybvoyager;
   ```

1. Create a trigger to set change current schema whenever the `ybvoyager` user connects:

   ```sql
   CREATE OR REPLACE TRIGGER ybvoyager.after_logon_trg
   AFTER LOGON ON ybvoyager.SCHEMA
   BEGIN
       DBMS_APPLICATION_INFO.set_module(USER, 'Initialized');
       EXECUTE IMMEDIATE 'ALTER SESSION SET current_schema=<SCHEMA_NAME>';
   END;
   ```

1. [OPTIONAL] Grant `SELECT_CATALOG_ROLE` to `ybvoyager`. This role might be required in migration planning and debugging.

   ```sql
   GRANT SELECT_CATALOG_ROLE TO ybvoyager;
   ```

   The `ybvoyager` user can now be used for migration.

1. You'll need to provide the user and the source database details in the subsequent invocations of yb-voyager. For convenience, you can populate the information in the following environment variables:

   ```sh
   export SOURCE_DB_TYPE=oracle
   export SOURCE_DB_HOST=localhost
   export SOURCE_DB_PORT=1521
   export SOURCE_DB_USER=ybvoyager
   export SOURCE_DB_PASSWORD=password
   export SOURCE_DB_NAME=source_db_name
   export SOURCE_DB_SCHEMA=source_schema_name
   ```
