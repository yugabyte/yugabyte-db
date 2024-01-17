---
title: DROP TABLESPACE statement [YSQL]
headerTitle: DROP TABLESPACE
linkTitle: DROP TABLESPACE
description: Use the DROP TABLESPACE statement to remove a tablespace from the cluster.
menu:
  preview:
    identifier: ddl_drop_tablespace
    parent: statements
aliases:
  - /preview/api/ysql/commands/ddl_drop_tablespace/
type: docs
---

## Synopsis

Use the `DROP TABLESPACE` statement to remove a tablespace from the cluster.

## Syntax

{{%ebnf%}}
  drop_tablespace
{{%/ebnf%}}

## Semantics

- Only the owner or a superuser can drop a tablespace. 
- Before dropping it, ensure that the tablespace is devoid of all database objects. 
- Be aware that even if the current database isn't using the tablespace, objects from other databases might still occupy it. 
- Additionally, the DROP operation may encounter issues if the tablespace is specified in the `temp_tablespaces` setting of any active session, as there could be temporary files or objects present in the tablespace.

### *if_exists*

Under normal operation, an error is raised if the tablespace does not exist.  Adding `IF EXISTS` will quietly ignore any non-existent tablespaces specified.

### *tablespace_name*

Specify the name of the tablespace to be dropped.

## Example

See [Tablespaces](../../../../../explore/ysql-language-features/going-beyond-sql/tablespaces/) and [Row Level Geo Partitioning](../../../../../explore/multi-region-deployments/row-level-geo-partitioning/) for full guides.

## See also

- [`CREATE TABLESPACE`](../ddl_create_tablespace)
