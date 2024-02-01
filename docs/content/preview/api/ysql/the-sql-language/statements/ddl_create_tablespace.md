---
title: CREATE TABLESPACE [YSQL]
headerTitle: CREATE TABLESPACE
linkTitle: CREATE TABLESPACE
description: Use the CREATE TABLESPACE statement to create a tablespace in the cluster.
menu:
  preview:
    identifier: ddl_create_tablespace
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE TABLESPACE` statement to create a tablespace in the cluster. It defines the tablespace name and tablespace properties.

## Syntax

{{%ebnf%}}
  create_tablespace
{{%/ebnf%}}

## Semantics

- Create a tablespace with *tablespace_name*. If `qualified_name` already exists in the cluster, an error will be raised.
- YSQL tablespaces allow administrators to specify the number of replicas for a table or index, and how they can be distributed across a set of clouds, regions, and zones in a geo-distributed deployment.

### *tablespace_option*

- Can be one of [`replica_placement`].
- Use `replica_placement` to specify the number of replicas stored in specific zones, regions, or clouds.

## Examples

See [Tablespaces](../../../../../explore/ysql-language-features/going-beyond-sql/tablespaces/) and [Row Level Geo Partitioning](../../../../../explore/multi-region-deployments/row-level-geo-partitioning/) for full guides.

## See also

- [`DROP TABLESPACE`](../ddl_drop_tablespace)
- [`CREATE TABLE`](../ddl_create_table)
- [`ALTER TABLE`](../ddl_alter_table)
