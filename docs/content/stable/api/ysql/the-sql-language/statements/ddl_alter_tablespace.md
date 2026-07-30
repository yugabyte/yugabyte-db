---
title: ALTER TABLESPACE [YSQL]
headerTitle: ALTER TABLESPACE
linkTitle: ALTER TABLESPACE
description: Use the ALTER TABLESPACE statement to change tablespace placement options.
menu:
  stable_api:
    identifier: ddl_alter_tablespace
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER TABLESPACE` statement to change the placement options of an existing tablespace.

Changing YugabyteDB placement options affects all YSQL tables, indexes, materialized views, and tablegroups that use the tablespace. The catalog change is applied immediately, but tablet replica movement is asynchronous and is performed by the load balancer.

## Syntax

```sql
ALTER TABLESPACE tablespace_name SET ( tablespace_option = value [, ...] );
ALTER TABLESPACE tablespace_name RESET ( tablespace_option [, ...] );
```

## Semantics

### tablespace_name

Specify the name of the tablespace to alter.

### tablespace_option

Can be one of `replica_placement` or `read_replica_placement`.

- Use `replica_placement` to specify the number of live replicas and how they are distributed across clouds, regions, and zones.
- Use `read_replica_placement` to specify read replica placement. `read_replica_placement` requires `replica_placement` to be set.

When a tablespace is already used by existing database objects, the new placement must be satisfiable by the currently available tablet servers. If the tablespace is unused, you can define placement before adding the corresponding tablet servers, matching `CREATE TABLESPACE` behavior.

## Examples

Change the live replica placement for a tablespace:

```sql
ALTER TABLESPACE us_west_tablespace SET (
  replica_placement = '{"num_replicas":3,
    "placement_blocks":[
      {"cloud":"cloud1","region":"us-west","zone":"us-west-1a","min_num_replicas":1},
      {"cloud":"cloud1","region":"us-west","zone":"us-west-1b","min_num_replicas":1},
      {"cloud":"cloud1","region":"us-west","zone":"us-west-1c","min_num_replicas":1}
    ]}'
);
```

Remove custom read replica placement:

```sql
ALTER TABLESPACE us_west_tablespace RESET (read_replica_placement);
```

Remove all custom placement options so objects using the tablespace fall back to the cluster placement policy:

```sql
ALTER TABLESPACE us_west_tablespace RESET (replica_placement, read_replica_placement);
```

## Notes

- Data movement is asynchronous. Monitor tablet placement from the YB-Master UI at `http://<YB-Master-host>:7000/tables`.
- The command updates the tablespace definition. You don't need to run `ALTER TABLE ... SET TABLESPACE` for each object already using the tablespace.
- Tablespaces are global objects. In xCluster or backup/restore workflows, ensure the expected tablespace definitions exist where they are needed.

## See also

- [CREATE TABLESPACE](../ddl_create_tablespace)
- [DROP TABLESPACE](../ddl_drop_tablespace)
- [ALTER TABLE](../ddl_alter_table)
- [ALTER INDEX](../ddl_alter_index)
