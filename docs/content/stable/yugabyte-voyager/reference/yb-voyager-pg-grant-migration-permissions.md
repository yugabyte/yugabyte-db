---
title: PostgreSQL migration permissions grant script
linktitle: Permissions SQL script
description: Usage, actions, and key considerations for the `yb-voyager-pg-grant-migration-permissions.sql` script.
headcontent: Run the permissions SQL script to grant user permissions for migration
menu:
  stable_yugabyte-voyager:
    identifier: pg-grant-perm
    parent: yb-voyager-misc
    weight: 120
type: docs
---

Use the `yb-voyager-pg-grant-migration-permissions.sql` script to configure a user (typically ybvoyager) on the source PostgreSQL database with the appropriate permissions required for running a migration, whether offline or live. The script ensures that the user has the necessary access to schemas, tables, sequences, and replication settings.

The script is located in `/opt/yb-voyager/guardrails-scripts/` or, for brew, check in `$(brew --cellar)/yb-voyager@<voyagerversion>/<voyagerversion>`.

## How to use

Run the script using [psql](https://www.postgresql.org/docs/current/app-psql.html), passing the required parameters in the following format:

```sql
psql -h <host> -d <database> -U <username> \
  -v voyager_user='<voyager_user>' \
  -v schema_list='<schema_list>' \
  -v is_live_migration=<is_live_migration> \
  -v is_live_migration_fall_back=<is_live_migration_fall_back> \
  -v replication_group='<replication_group>' \
  -f <path_to_script>
```

### Parameters

| Parameter | Description |
| :--- | :--- |
| `<host>` | Hostname of the PostgreSQL server. |
| `<database>` | The target YuagbyteDB database name. |
| `<username>` | User running the script (must have sufficient privileges). |
| `<voyager_user>` | The migration user (for example, ybvoyager) that permissions are granted to. |
| `<schema_list>` | A comma-separated list of schemas (for example, `schema1`, `public`, `schema2`). |
| `<is_live_migration>` | `1` for live migration, `0` for offline migration. |
| `<is_live_migration_fall_back>` | `1` for live migration with fallback, `0` for fall-forward. Only applicable if live migration is enabled. |
| `<replication_group>` | Replication group name. Required only for live migration. |
| `<path_to_script>` | Path to the script file. |

### Examples

```sql
psql -h localhost -d mydb -U admin \
  -v voyager_user='ybvoyager' \
  -v schema_list='schema1,public,schema2' \
  -v is_live_migration=1 \
  -v is_live_migration_fall_back=0 \
  -v replication_group='replication_group' \
  -f yb-voyager-pg-grant-migration-permissions.sql
```

**Offline migration**

```sql
psql -h myhost -d mydb -U admin \
  -v voyager_user='ybvoyager' \
  -v schema_list='public' \
  -v is_live_migration=0 \
  -f yb-voyager-pg-grant-migration-permissions.sql
```

**Live migration with replication group**

```sql
psql -h myhost -d mydb -U admin \
  -v voyager_user='ybvoyager' \
  -v schema_list='public,sales' \
  -v is_live_migration=1 \
  -v is_live_migration_fall_back=0 \
  -v replication_group='voyager_repl' \
  -f yb-voyager-pg-grant-migration-permissions.sql
```

**Live migration with fall-back and replication group**

```sql
psql -h myhost -d mydb -U admin \
  -v voyager_user='ybvoyager' \
  -v schema_list='public' \
  -v is_live_migration=1 \
  -v is_live_migration_fall_back=1 \
  -v replication_group='voyager_repl' \
  -f yb-voyager-pg-grant-migration-permissions.sql
```

## Script actions

The script performs the following actions when run.

### Parameter validation

- Ensures all required parameters are provided.
- Validates relationships between parameters (for eaxmple, replication group required for live migration).
- Exits with an error if required flags are missing.

### Basic permissions

- Grants USAGE on all schemas to `voyager_user` parameter.
- Grants SELECT on all tables and sequences across schemas.
- Grants `pg_read_all_stats` role to access `pg_stat_statements`.

### Live migration actions

If live migration is enabled, the script performs the following actions.

**Replica identity changes**

Sets `REPLICA IDENTITY FULL` on all tables in the specified schemas to ensure row images are available for replication.

**Replication Permissions**

- On RDS: Grants `rds_replication` to `voyager_user`.
- On non-RDS: Alters `voyager_user` with REPLICATION privilege.

**Ownership handling options**

When prompted, you choose one of two strategies:

- Option 1: Replication group

  - Creates a replication group role.
  - Adds both the original table owners and `voyager_user` to the group.
  - Transfers ownership of all relevant tables to the group.

- Option 2: Direct role grants

  - Grants roles of original owners directly to `voyager_user`.

**Database Permissions**

Grants CREATE privilege on the database to `voyager_user`.

### Live migration with fall-back actions

If live migration with fall-back is enabled, the script performs the following actions:

- Grants additional INSERT, UPDATE, DELETE on tables.
- Grants USAGE, UPDATE on sequences.
- If PostgreSQL version is 15 or later, grants SET privilege on `session_replication_role`.

## Key considerations

- On RDS, `Permission Denied` errors for system schemas (for example, pg_statistic) can be ignored; they do not impact migrations.

- Ensure `voyager_user` exists before running the script.

- The script requires elevated privileges (a superuser or equivalent role).

- For live migration, carefully decide between replication group ownership transfer and direct role grants:

  - Replication groups are more maintainable in large environments.

  - Direct grants may be simpler for smaller migrations.

- Live migration with fall-back requires additional privileges, which the script applies.

- On PostgreSQL 15 or later, `session_replication_role` privileges are explicitly granted in the case of live migration with fall-back.
