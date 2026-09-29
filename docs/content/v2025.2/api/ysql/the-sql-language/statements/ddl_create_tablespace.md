---
title: CREATE TABLESPACE [YSQL]
headerTitle: CREATE TABLESPACE
linkTitle: CREATE TABLESPACE
description: Use the CREATE TABLESPACE statement to create a tablespace in the cluster.
menu:
  v2025.2_api:
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

#### replica_placement

`replica_placement` is a JSON object that places the replicas of tables and indexes in this tablespace.

- `num_replicas` is the replication factor. It is required and must be a positive integer.
- `placement_blocks` is an array of placement blocks. Each block requires `cloud`, `region`, `zone`, and `min_num_replicas`.
  - `min_num_replicas` is the minimum number of replicas in that cloud, region, and zone. It must be greater than 0. The sum across blocks cannot exceed `num_replicas`.
  - `region` and `zone` accept `*` for any region or any zone. A `*` region requires a `*` zone. `cloud` must name a cloud.
  - `leader_preference` is an optional positive integer. `1` is the most preferred zone for tablet leaders. Values that are set must form a contiguous sequence starting at 1. Zones that share a value split leaders evenly. Zones that omit the field are least preferred.

#### read_replica_placement

{{<tags/feature/ea idea="2006">}}`read_replica_placement` is a JSON object for copies on a [read replica](../../../../../explore/multi-region-deployments/read-replicas-ysql/) cluster. It uses the same fields as `replica_placement`. A tablespace has one read-replica placement. `leader_preference` applies only to `replica_placement`.

`placement_uuid` is the [placement ID of the read replica cluster](../../../../../admin/yb-admin/#add-read-replica-placement-info). When the cluster has one read-replica cluster, omit `placement_uuid` and that cluster's placement ID is used. When the cluster has more than one, set `placement_uuid` to the cluster you want. Obtain the ID with [get-universe-config](../../../../../admin/yb-admin/#get-universe-config).

## Examples

See [Tablespaces](../../../../../explore/going-beyond-sql/tablespaces/) and [Row Level Geo Partitioning](../../../../../explore/multi-region-deployments/row-level-geo-partitioning/) for full guides.

## See also

- [ALTER TABLESPACE](../ddl_alter_tablespace)
- [DROP TABLESPACE](../ddl_drop_tablespace)
- [CREATE TABLE](../ddl_create_table)
- [ALTER TABLE](../ddl_alter_table)
