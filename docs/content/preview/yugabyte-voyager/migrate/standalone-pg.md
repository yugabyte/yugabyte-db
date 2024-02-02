<!--
+++
private=true
+++
-->

1. yb_voyager requires `wal_level` to be logical. You check this using following the steps:

    1. Run the command `SHOW wal_level` on the database to check the value.

    1. If the value is anything other than logical, run the command `SHOW config_file` to know the path of your configuration file.

    1. Modify the configuration file by uncommenting the parameter `wal_level` and set the value to logical.

    1. Restart PostgreSQL.

1. Check that the replica identity is "FULL" for all tables on the database.

    1. Run the following query to check the replica identity:

        ```sql
        SELECT relname, relreplident
        FROM pg_class
        WHERE relnamespace = (SELECT oid FROM pg_namespace WHERE nspname = 'your_schema_name') AND relkind = 'r';
        ```

    1. Run the following query to change the replica identity of all tables if the tables have an identity other than FULL (`f`):

        ```sql
        DO $$
        DECLARE
          table_name_var text;
        BEGIN
          FOR table_name_var IN (SELECT table_name FROM information_schema.tables WHERE table_schema = '<your_schema>' AND table_type = 'BASE TABLE')
          LOOP
            EXECUTE 'ALTER TABLE ' || table_name_var || ' REPLICA IDENTITY FULL';
          END LOOP;
        END $$;
        ```

1. Create user `ybvoyager` for the migration using the following command:

    ```sql
    CREATE USER ybvoyager PASSWORD 'password' REPLICATION;
    ```

1. Switch to the database that you want to migrate.

   ```sql
   \c <database_name>
   ```

1. Grant the `USAGE` permission to the `ybvoyager` user on all schemas of the database.

   ```sql
   SELECT 'GRANT USAGE ON SCHEMA ' || schema_name || ' TO ybvoyager;' FROM information_schema.schemata; \gexec
   ```

   The above `SELECT` statement generates a list of `GRANT USAGE` statements which are then executed by `psql` because of the `\gexec` switch. The `\gexec` switch works for PostgreSQL v9.6 and later. For older versions, you'll have to manually execute the `GRANT USAGE ON SCHEMA schema_name TO ybvoyager` statement, for each schema in the source PostgreSQL database.

1. Grant `SELECT` permission on all the tables and sequences.

   ```sql
   SELECT 'GRANT SELECT ON ALL TABLES IN SCHEMA ' || schema_name || ' TO ybvoyager;' FROM information_schema.schemata; \gexec

   SELECT 'GRANT SELECT ON ALL SEQUENCES IN SCHEMA ' || schema_name || ' TO ybvoyager;' FROM information_schema.schemata; \gexec
   ```

   The `ybvoyager` user can now be used for migration.

1. Create a replication group.

    ```sql
    CREATE ROLE REPLICATION_GROUP;
    ```

1. Add the original owner of the table to the group.

    ```sql
    GRANT REPLICATION_GROUP TO <original_owner>;
    ```

1. Add the user `ybvoyager` to the replication group.

    ```sql
    GRANT REPLICATION_GROUP TO ybvoyager;
    ```

1. Transfer ownership of the tables to <replication_group>.