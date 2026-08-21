# MotherDuck Integration

## Connect with MotherDuck

`pg_duckdb` integrates natively with [MotherDuck][md]. To enable this support, you first need to [generate an access token][md-access-token]. Then, you can enable it using the `duckdb.enable_motherduck` convenience function:

```sql
-- If not provided, the token will be read from the `motherduck_token` environment variable
-- If not provided, the default MD database name is `my_db`
CALL duckdb.enable_motherduck('<optional token>', '<optional MD database name>');
```

This function creates a `motherduck` `SERVER` using the `pg_duckdb` Foreign Data Wrapper, which hosts the options for this integration. It also provides a `USER MAPPING` for the current user, which stores the provided MotherDuck token (if any).

> Important: once MotherDuck is enabled, calling `enable_motherduck` again with a different database parameter will **not** change the default database. Instead, it will return a notice saying "MotherDuck is already enabled" and ignore the new database parameter. To change the default database, see [Changing the Default Database](#changing-the-default-database) below. Alternatively, you can access any MotherDuck database without changing the default using the `ddb$<database>` schema syntax described in [Schema Mapping](#schema-mapping).

You can refer to the [Advanced MotherDuck Configuration](#advanced-motherduck-configuration) section below for more on the `SERVER` and `USER MAPPING` configuration.

### Non-Superuser Configuration

If you want to use MotherDuck as a non-superuser, you also have to configure the `duckdb.postgres_role` setting:

```ini
# Changing this requires a Postgres restart
duckdb.postgres_role = 'your_role_name'  # e.g., duckdb or duckdb_group
```

You also need to ensure that this role has `CREATE` permissions on the `public` schema in Postgres, as this is where tables from the MotherDuck `main` schema are created. You can grant these permissions as follows:

```sql
GRANT CREATE ON SCHEMA public TO {your_role_name};
-- So if you've configured the duckdb role above
GRANT CREATE ON SCHEMA public TO duckdb;
```

If you grant these permissions after starting Postgres, the initial sync of MotherDuck tables may have failed for the `public` schema. You can force a full resync of the tables by running:

```sql
SELECT * FROM pg_terminate_backend((
  SELECT pid FROM pg_stat_activity WHERE backend_type = 'pg_duckdb sync worker'
));
```

## Using MotherDuck with `pg_duckdb`

After completing the configuration (and possibly restarting Postgres in case you changed `duckdb.postgres_role`), you can create tables in your MotherDuck database using the `duckdb` [Table Access Method (TAM)][tam]:

```sql
CREATE TABLE orders(id bigint, item text, price NUMERIC(10, 2)) USING duckdb;
CREATE TABLE users_md_copy USING duckdb AS SELECT * FROM users;
```

[tam]: https://www.postgresql.org/docs/current/tableam.html

Any tables that you already had in MotherDuck are automatically available in Postgres. Since DuckDB and MotherDuck allow accessing multiple databases from a single connection and Postgres does not, we map database+schema in DuckDB to a schema name in Postgres.

The default MotherDuck database will be easiest to use (see below for details), by default this is `my_db`.

## Advanced MotherDuck Configuration

If you want to specify which MotherDuck database is your default, you need to configure MotherDuck using a `SERVER` and a `USER MAPPING`:

```sql
CREATE SERVER motherduck
TYPE 'motherduck'
FOREIGN DATA WRAPPER duckdb
OPTIONS (default_database '<your database>');

-- You may use `::FROM_ENV::` to have the token be read from the environment variable
CREATE USER MAPPING FOR CURRENT_USER SERVER motherduck OPTIONS (token '<your token>')
```

Note: The `duckdb.enable_motherduck` function simplifies this process:
```sql
CALL duckdb.enable_motherduck('<token>', '<default database>');
```

### Changing the Default Database

If you need to change the default MotherDuck database after initially enabling it, you must first drop the `motherduck` server and then reconnect:

```sql
-- Drop the existing connection (this will CASCADE drop all synced tables/schemas)
DROP SERVER motherduck CASCADE;

-- Reconnect with a different default database
CALL duckdb.enable_motherduck('<token>', '<new_default_database>');
```

Important: using `DROP SERVER motherduck CASCADE` will remove all schemas and tables that were synced from MotherDuck. They will be automatically recreated by the background worker after reconnecting.

Alternative: if you only need to query tables from a different database occasionally, you don't need to change the default database at all. You can use the `ddb$<database>` schema prefix to access any MotherDuck database simultaneously. See the [Schema Mapping](#schema-mapping) section below for details and examples.

## Schema Mapping

DuckDB and Postgres have different schema and database conventions. While you specify a _default_ MotherDuck database when connecting, you can access tables from **any** MotherDuck database in your account using the schema mapping described below.

The mapping from a DuckDB `database.schema` to a Postgres schema is done as follows:

1. The `main` DuckDB schema in your default database is merged with the Postgres `public` schema.
2. All other schemas in your default MotherDuck database is merged with the Postgres schema of the same name.
3. The `main` schema in other databases can be accessed using the name `ddb$<db_name>` (including the literal `$` character).
4. All other schemas in non-default MotherDuck databases are accessible through `ddb$<duckdb_db_name>$<duckdb_schema_name>` (including the literal `$` characters).

An example of each of these cases is shown below:

```sql
-- Assuming my_db is your default database:

-- Case 1: Default database main schema -> public
INSERT INTO my_table VALUES (1, 'abc'); -- inserts into my_db.main.my_table

-- Case 2: Default database other schemas -> same schema name
INSERT INTO your_schema.tab1 VALUES (1, 'abc'); -- inserts into my_db.your_schema.tab1

-- Case 3: Other database main schema -> ddb$<db_name>
SELECT COUNT(*) FROM ddb$my_shared_db.aggregated_order_data; -- reads from my_shared_db.main.aggregated_order_data

-- Case 4: Other database other schemas -> ddb$<db_name>$<schema_name>
SELECT COUNT(*) FROM ddb$sample_data$hn.hacker_news; -- reads from sample_data.hn.hacker_news
```

**Example: Accessing multiple databases in a single query:**

```sql
-- Join data from your default database (my_db) with sample_data
SELECT
    orders.order_id,
    orders.customer_name,
    hn.title as related_article
FROM my_table orders  -- my_db.main.my_table (default database)
JOIN ddb$sample_data$hn.hacker_news hn  -- sample_data.hn.hacker_news (different database)
    ON orders.topic = hn.type
WHERE orders.created_at > '2024-01-01';
```

## Switching MotherDuck Tokens

To switch your MotherDuck Token, first call:
`DROP SERVER MOTHERDUCK CASCADE;` and then reinitialize with a new token: `CALL duckdb.enable_motherduck('new_token');`

## Simple Read-scaling Setup

pg_duckdb supports [read-scaling tokens for MotherDuck](https://motherduck.com/docs/key-tasks/authenticating-and-connecting-to-motherduck/read-scaling), you can simply use a read-scaling token as the token when enabling MotherDuck:

```sql
CALL duckdb.enable_motherduck('<your_read_scaling_token>', '<database_name>');
```

You can create a read-scaling token in the [MotherDuck UI](https://motherduck.com/docs/key-tasks/authenticating-and-connecting-to-motherduck/) by selecting "Read Scaling Token" as the token type.

## Read-scaling setup with Read Write Setup

For applications that need both read and write access from the same PostgreSQL instance, you can configure different PostgreSQL users with different MotherDuck tokens. In this case the server owner should get a write token, so it can sync the latest versions of tables from MotherDuck. Any Postgres users that should use read-scaling should get a read-scaling token.

### Step 1: Create the MotherDuck server with a write token

```sql
-- Create the server (no token here)
CALL duckdb.enable_motherduck('<your_write_token>', '<your_database_name>');
```

### Step 2: Create a read-only user with a read-scaling token

```sql
-- Create a PostgreSQL user for read-only access
CREATE USER reader_user IN ROLE duckdb_group;

-- Give them a read-scaling token
CREATE USER MAPPING FOR reader_user
    SERVER motherduck
    OPTIONS (token '<your_read_scaling_token>');
```

### Usage

```sql
-- As the server owner: full read-write access
INSERT INTO my_table VALUES (1, 'data');
SELECT * FROM my_table;

-- As reader_user: read-only access that scales across read-scaling ducklings
SELECT * FROM my_table;  -- works
INSERT INTO my_table VALUES (2, 'more');  -- fails with read-only error
```


## Debugging

If some tables or schemas are not appearing as expected, check your Postgres log file. The background worker that automatically syncs tables may have encountered an error, which will be reported in the logs, often with information on how to resolve the issue.

[md]: https://motherduck.com/
[md-access-token]: https://motherduck.com/docs/key-tasks/authenticating-and-connecting-to-motherduck/authenticating-to-motherduck/#authentication-using-an-access-token
