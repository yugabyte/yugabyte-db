---
title: Parallel queries
linkTitle: Parallel queries
description: Parallel queries in YSQL
tags:
  feature: early-access
menu:
  v2025.1:
    identifier: advanced-features-parallel-query
    parent: additional-features
    weight: 60
type: docs
---

YugabyteDB supports the use of [PostgreSQL parallel queries](https://www.postgresql.org/docs/15/parallel-query.html). Using parallel queries, the [query planner](../../architecture/query-layer/planner-optimizer/) can devise plans that leverage multiple CPUs to answer queries faster.

Currently, YugabyteDB supports parallel queries for [colocated tables](../colocation/); support for hash- and range-sharded tables is planned.

To enable and configure parallel queries, set the following configuration parameters.

| Parameter | Description | Default |
| :--- | :--- | :--- |
| yb_enable_parallel_append | Enables the planner's use of parallel append plans. To enable parallel query, set this to true. | false |
| yb_parallel_range_rows | The number of rows to plan per parallel worker. To enable parallel query, set this to a value other than 0. (Recommended: 10000) | 0 |
| yb_parallel_range_size | Approximate size of parallel range for DocDB relation scans. | 1MB |

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
