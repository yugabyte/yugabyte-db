

Create a database user and provide the user with READ access to all the resources which need to be migrated. Run the following commands in a `psql` session:

- Create a new user named `ybmigrate`.

```psql
CREATE USER ybmigrate PASSWORD 'password';
```

- Switch to the database that you want to migrate.

```psql
\c <database_name>
```

- Grant the `USAGE` permission to the `ybmigrate` user on all schemas of the database.

```psql
SELECT 'GRANT USAGE ON SCHEMA ' || schema_name || ' TO ybmigrate;' FROM information_schema.schemata; \gexec
```

The above `SELECT` statement generates a list of `GRANT USAGE` statements which are then executed by `psql` because of the `\gexec` switch.

- Grant `SELECT` permission on all the tables and sequences.

```psql
SELECT 'GRANT SELECT ON ALL TABLES IN SCHEMA ' || schema_name || ' TO ybmigrate;' FROM information_schema.schemata; \gexec

SELECT 'GRANT SELECT ON ALL SEQUENCES IN SCHEMA ' || schema_name || ' TO ybmigrate;' FROM information_schema.schemata; \gexec
```

Now you can use the `ybmigrate` user for migration.

- You'll need to provide the user and the source database details in the subsequent invocations of yb_migrate. For convenience, you can populate the information in the following environment variables:

```sh
export SOURCE_DB_TYPE=postgresql
export SOURCE_DB_HOST=localhost
export SOURCE_DB_PORT=1521
export SOURCE_DB_USER=ybmigrate
export SOURCE_DB_PASSWORD=password
export SOURCE_DB_NAME=pdb1
export SOURCE_DB_SCHEMA=sakila
```
