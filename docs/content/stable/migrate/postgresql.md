
Create a database user and provide the user with READ access to all the resources which need to be migrated. Run the following commands in a `psql` session:

1. Create a new user named `ybvoyager`.

   ```sql
   CREATE USER ybvoyager PASSWORD 'password';
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

1. You'll need to provide the user and the source database details in the subsequent invocations of yb-voyager. For convenience, you can populate the information in the following environment variables:

   ```sh
   export SOURCE_DB_TYPE=postgresql
   export SOURCE_DB_HOST=localhost
   export SOURCE_DB_USER=ybvoyager
   export SOURCE_DB_PASSWORD=password
   export SOURCE_DB_NAME=source_db_name
   ```
