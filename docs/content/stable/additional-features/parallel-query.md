---
title: Parallel queries
linkTitle: Parallel queries
description: Parallel queries in YSQL
menu:
  stable:
    identifier: advanced-features-parallel-query
    parent: additional-features
    weight: 60
aliases:
  - /stable/explore/ysql-language-features/advanced-features/parallel-queries/
type: docs
---

YugabyteDB supports the use of [PostgreSQL parallel queries](https://www.postgresql.org/docs/15/parallel-query.html). Using parallel queries, the [query planner](../../architecture/query-layer/planner-optimizer/) can devise plans that leverage multiple CPUs to answer queries faster.

YugabyteDB supports parallel queries for [colocated](../colocation/), hash-, and range-sharded tables.

To configure parallel queries, set the following configuration parameters.

| Parameter | Description | Default |
| :--- | :--- | :--- |
| yb_enable_parallel_append | Enables the planner's use of parallel append plans. This flag is an alias for the `enable_parallel_append` PostgreSQL parameter. For more information, refer to [Parallel Append](https://www.postgresql.org/docs/15/parallel-plans.html#PARALLEL-APPEND) in the PostgreSQL documentation. | false |
| yb_enable_parallel_scan_colocated | Enables the planner's use of parallel queries for colocated tables. | false |
| yb_enable_parallel_scan_hash | Enables the planner's use of parallel queries for hash-sharded tables. | false |
| yb_enable_parallel_scan_range | Enables the planner's use of parallel queries for range-sharded tables. | false |
| yb_parallel_range_rows | The number of rows to plan per parallel worker. | 0 |
| yb_parallel_range_size | Approximate size of parallel range for DocDB relation scans. Numeric with memory unit (B, kB, MB, or GB). | 1MB |

In addition, you can use the following PostgreSQL configuration parameters to configure parallel queries:

- Optimize the number of workers used by the parallel query.
  - [max_parallel_workers](https://www.postgresql.org/docs/15/runtime-config-resource.html#GUC-MAX-PARALLEL-WORKERS)
  - [max_parallel_workers_per_gather](https://www.postgresql.org/docs/15/runtime-config-resource.html#GUC-MAX-PARALLEL-WORKERS-PER-GATHER)
  - [max_parallel_maintenance_workers](https://www.postgresql.org/docs/15/runtime-config-resource.html#GUC-MAX-PARALLEL-WORKERS-MAINTENANCE)
- Optimize cost of parallel plan to achieve the optimal plan.
  - [parallel_setup_cost](https://www.postgresql.org/docs/15/runtime-config-query.html#GUC-PARALLEL-SETUP-COST)
  - [parallel_tuple_cost](https://www.postgresql.org/docs/15/runtime-config-query.html#GUC-PARALLEL-TUPLE-COST)
- Enable or disable the query planner's use of hash-join plan types with parallel hash. Has no effect if hash-join plans are not also enabled. The default is on.
  - [enable_parallel_hash](https://www.postgresql.org/docs/15/runtime-config-query.html#RUNTIME-CONFIG-QUERY-ENABLE)

For more information, refer to [How Parallel Query Works](https://www.postgresql.org/docs/15/how-parallel-query-works.html) in the PostgreSQL documentation.

## Enable parallel query

To enable parallel query, set the following parameters:

- yb_enable_parallel_append: `true`.
- yb_enable_parallel_scan_colocated, yb_enable_parallel_scan_hash, and/or yb_enable_parallel_scan_range: `true`.
- yb_parallel_range_rows: a value other than 0 (10000 recommended).

Parallel append is enabled in tandem with the [cost-based optimizer](../../best-practices-operations/ysql-yb-enable-cbo/) (CBO). When you set CBO to `on`, parallel append is enabled as follows:

- yb_enable_parallel_append is set to `true`.
- yb_parallel_range_rows is set to `10000`.

Note that when upgrading a deployment to v2025.2 or later, if the universe has CBO enabled (`on`), parallel append is automatically enabled.

For information on CBO, refer to [Enable cost-based optimizer](../../best-practices-operations/ysql-yb-enable-cbo/).
