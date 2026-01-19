---
title: Import data without a superuser
linktitle: Non-superuser data import
description: Importing data without a superuser in YugabyteDB
menu:
  stable_yugabyte-voyager:
    identifier: yb-voyager-superuser
    parent: yb-voyager-misc
    weight: 120
type: docs
---

This document provides a step-by-step guide to importing data into YugabyteDB without using a superuser account. It outlines why superuser access is required in some cases, how to work around it, and the precise steps to follow to import data cleanly.

## Why do you need a Superuser?

Superuser access is primarily needed to:

- Set `session_replication_role` to `replica` (used to temporarily disable triggers and foreign keys during import).

- Create extensions like pgcrypto.

- Create extensions like [postgres_fdw](https://www.postgresql.org/docs/current/postgres-fdw.html) or [dblink](https://www.postgresql.org/docs/current/dblink.html).

  - These extensions define foreign data wrappers and access mechanisms that interact with external databases.
  - They register C functions and system-level handlers — only superusers can install them.

- Create custom base types.

  - Base types (`CREATE TYPE ...`) require defining low-level behaviors (such as INPUT and OUTPUT functions).
  - These are internally linked to system operations and can affect core engine behavior.

- Use of `LANGUAGE internal` in functions.

  - Functions using `LANGUAGE internal` map directly to compiled C functions (like `boolin`).

- Any other unknown cases.

## Import without a Superuser

Use the following guidance to import data into YugabyteDB without using an account with superuser privileges.

### Grant permissions

To grant the necessary permissions on the target YugabyteDB universe, do the following:

- In v2025.1 and later (which is based on PostgreSQL 15), grant the following permission:

    ```sql
    GRANT SET ON PARAMETER session_replication_role TO <username>;
    ```

    Granting this permission eliminates the need to manually disable or drop foreign keys and triggers. You may still encounter errors during schema import when creating extensions (for example, hstore), which require a superuser because their install scripts perform superuser-only actions (such as ALTER TYPE).

    Note that you still need to grant the following permissions in addition to granting the `SET ON PARAMETER sessions_replication_role`.

- For all versions, grant the following permissions:

    ```sql
    -- Grant CREATE ON DATABASE
    GRANT CREATE ON DATABASE <db_name> TO <username>;

    -- Grant USAGE, CREATE ON SCHEMA(s)
    GRANT USAGE, CREATE ON SCHEMA <schema_name> TO <username>;

    -- Alter the OWNER of the SCHEMA(s)
    ALTER SCHEMA <schema_name> OWNER TO <username>;

    -- Grant extension creation role (for pgcrypto, etc.)
    GRANT yb_extension TO <username>;
    ```

### Guardrail errors

If you face guardrail errors (permission-related) during import, enter "yes" to allow schema/data import to proceed despite the errors.

These will be mostly related to `session_replication_role` when running versions earlier than v2025.1 (that is, `SET ON PARAMETER sessions_replication_role` has not been granted). In these cases, as foreign keys and triggers will be [handled manually](#import-data-errors), you can safely ignore these errors.

### Import schema errors

In complex cases, schema import may fail because some objects can only be created by a superuser. This can happen on both older and newer YugabyteDB versions (including v2025.1 and later). For example, installing extensions such as hstore may fail as their install scripts include _superuser-only_ operations (like ALTER TYPE) or other catalog-level actions.

In such cases, do the following:

1. Run the import schema command with the `--continue-on-error` flag.

    All failed SQL statements will be collected in `<export-dir>/schema/failed.sql`.

1. Review and execute these failed statements manually on the target YugabyteDB using a superuser (or pre-create the required extensions as admin before running import schema).

### Import data errors

During data import, foreign keys and triggers can cause failures or significantly slow performance because they enforce referential integrity and execute additional logic when rows are being inserted.

To mitigate the failures, temporarily disable these triggers before running the import, and restore them afterwards.

(This is not necessary if you are using v2025.1 and later and granted `SET ON PARAMETER sessions_replication_role`.)

If you're using an older version (pre-PostgreSQL 15 and YugabyteDB v2025.1), do the following:

1. Disable triggers as follows:

    ```sql
    DO $$
    DECLARE
        r RECORD;
    BEGIN
        FOR r IN
            SELECT table_schema, quote_ident(table_name) AS table_name
            FROM information_schema.tables
            WHERE table_type = 'BASE TABLE'
              AND table_schema IN (<SCHEMA_LIST>)
        LOOP
            EXECUTE format('ALTER TABLE %I.%I DISABLE TRIGGER ALL;', r.table_schema, r.table_name);
        END LOOP;
    END $$;
    ```

1. Drop foreign keys.

    Manually drop all foreign keys on the target YugabyteDB database before starting the import. See [Extract, drop, and recreate foreign keys](#extract-drop-and-recreate-foreign-keys) for details.

1. Run import data.

    After disabling triggers and dropping foreign keys, run import data to load the records without constraint checks interfering.

1. Re-enable triggers as follows:

    ```sql
    DO $$
    DECLARE
        r RECORD;
    BEGIN
        FOR r IN
            SELECT table_schema, quote_ident(table_name) AS table_name
            FROM information_schema.tables
            WHERE table_type = 'BASE TABLE'
              AND table_schema IN (<SCHEMA_LIST>)
        LOOP
            EXECUTE format('ALTER TABLE %I.%I ENABLE TRIGGER ALL;', r.table_schema, r.table_name);
        END LOOP;
    END $$;
    ```

1. Recreate foreign keys.

   Reapply the original foreign key constraints after import. See [Extract, drop, and recreate foreign keys](#extract-drop-and-recreate-foreign-keys) on how to reapply them.

## Extract, drop, and recreate foreign keys

This section describes how to extract foreign key constraints from a pg_dump file, generate drop statements, and recreate them post-import.

1. Extract foreign key statements.

   You extract the statements from the schema dump created after export schema.

    ```sh
    awk '
    /^ALTER TABLE/ && /ADD CONSTRAINT/ && /FOREIGN KEY/ && /;/ {
        print
    }
    ' <export-dir>/schema/tables/table.sql > filtered_schema.sql
    ```

1. Generate drop foreign key statements.

    ```sh
    awk '
    /^ALTER TABLE/ && /ADD CONSTRAINT/ && /FOREIGN KEY/ {
        if ($3 == "ONLY") {
         # Postgres style: ALTER TABLE ONLY public.table ADD CONSTRAINT ...
         table = $4
         constraint = $7
        } else {
         # Oracle/Mysql style: ALTER TABLE table ADD CONSTRAINT ...
         table = $3
         constraint = $6
         }
         gsub(";", "", constraint)   # remove trailing semicolon if any
         print "ALTER TABLE " table " DROP CONSTRAINT " constraint ";"
    }
    ' filtered_schema.sql > drop_fks.sql
    ```

1. Run `drop_fks.sql` on the target YugabyteDB database to drop all foreign keys.

1. Recreate foreign key constraints.

    You can re-apply the extracted foreign key statements to restore referential integrity. Re-run the `filtered_schema.sql` file:

    ```sql
    ysqlsh -d <db_name> -f filtered_schema.sql
    ```

    This command re-adds the foreign key constraints exactly as they were before. If validating all constraints upfront is too performance-intensive, you can instead create the constraints as NOT VALID. This defers validation but still enforces the constraint for any new data inserted after it is created.

    ```sql
    ALTER TABLE <table_name>
    ADD CONSTRAINT <constraint_name> FOREIGN KEY (<col>)
    REFERENCES <ref_table>(<ref_col>) NOT VALID;
    ```

    You can also run VALIDATE CONSTRAINT in a controlled manner to check existing data:

    ```sql
    ALTER TABLE <table_name> VALIDATE CONSTRAINT <constraint_name>;
    ```
