---
title: Query Planner
headerTitle: Query Planner / CBO
linkTitle: Query Planner
headcontent: Understand how the planner chooses the optimal path for query execution
tags:
  feature: early-access
menu:
  preview:
    identifier: query-planner
    parent: architecture-query-layer
    weight: 100
type: docs
rightnav:
  hideH4: true
---

The query planner is responsible for determining the most efficient way to execute a given query. The optimizer is the critical component in the planner that calculates the costs of different execution plans, taking into account factors like index lookups, table scans, network round trips and storage costs. It then selects the most cost-effective path for query execution. YugabyteDB implements completely different types of optimizers for the YSQL and YCQL APIs.

## Rule based optimizer (YCQL)

YugabyteDB implements a simple rules based optimizer (RBO) for YCQL. It operates by applying a predefined set of rules to optimize queries, such as reordering joins to minimize the number of rows processed, pushing selection conditions down the query tree, and utilizing indexes and views to enhance performance.

## Heuristics based optimizer (YSQL)

YugabyteDB’s YSQL API uses a simple heuristics based optimizer to determine the most efficient execution plan for a query. It relies on basic statistics, like table sizes, and applies heuristics to estimate the cost of different plans. The cost model is based on PostgreSQL’s approach, using data such as row counts and index availability and assigns some heuristic costs to the number of result rows depending on the type of the scan. Although this works well for most queries, because this model was designed for single-node databases like PostgreSQL, it doesn’t account for YugabyteDB’s distributed architecture or take cluster topology into consideration during query planning.

{{<tip>}}

The default CBO is {{<tags/feature/tp>}} and disabled by default. To enable it, turn ON the [yb_enable_optimizer_statistics](../../../reference/configuration/yb-tserver/#yb-enable-optimizer-statistics) configuration parameter as follows:

```sql
-- Enable for current session
SET yb_enable_optimizer_statistics = TRUE;
```

{{</tip>}}

## Cost based optimizer - CBO (YSQL)

To account for the distributed nature of the data, YugabyteDB has implemented a Cost based optimizer for YSQL that uses an advanced cost model that takes into consideration of accurate table statistics, the cost of network round trips, operations on lower level storage layer and the cluster toplogy. Let us see in detail how this works.

{{<tip>}}

The YugabyteDB CBO is {{<tags/feature/ea>}} and disabled by default. To enable it, turn ON the [yb_enable_base_scans_cost_model](../../../reference/configuration/yb-tserver/#yb-enable-base-scans-cost-model) configuration parameter as follows:

```sql
-- Enable for current session
SET yb_enable_base_scans_cost_model = TRUE;
```

{{</tip>}}

### Plan search algorithm

To optimize the search for the best plan, the CBO uses a dynamic programming-based algorithm. Instead of enumerating and evaluating the cost of each possible execution plan, it breaks the problem down and finds the most optimal sub-plans for each part of the query. The sub-plans are then combined to find the best overall plan.

### Statistics gathering

The optimizer relies on accurate statistics about the tables, including the number of rows, the distribution of data in columns, and the cardinality of results from operations. These statistics are essential for estimating the selectivity of filters and costs of various query plans accurately. These statistics are gathered by the [ANALYZE](../../../api/ysql/the-sql-language/statements/cmd_analyze/) command and are provided in a display-friendly format by the [pg_stats](../../../architecture/system-catalog/#data-statistics) view.

{{<note title="Run ANALYZE manually" >}}
Currently, YugabyteDB doesn't run a background job like PostgreSQL autovacuum to analyze the tables. To collect or update statistics, run the ANALYZE command manually. If you have enabled CBO, you must run ANALYZE on user tables after data load for the CBO to create optimal execution plans. Multiple projects are in progress to trigger this automatically.
{{</note>}}

### Cost estimation

For each potential execution plan, the optimizer calculates costs in terms of storage layer lookups both cache and disk, number of network round trips and other factors. These costs help the optimizer compare which plan would likely be the most efficient to execute given the current database state and query context.

{{<tip>}}
These estimates can be seen when using the DEBUG option in the [EXPLAIN](../../../api/ysql/the-sql-language/statements/perf_explain) command as EXPLAIN (ANALYZE, DEBUG).
{{</tip>}}

Some of the factors included in the cost estimation are discussed below.

1. **Data fetch**

    To estimate the cost of fetching a tuple from [DocDB](../../docdb/), the CBO takes into account factors such as the number of SST files that may need to be read, and the estimated number of [seeks](../../docdb/lsm-sst/#seek), [previous](../../docdb/lsm-sst/#previous), and [next](../../docdb/lsm-sst/#next) operations that may be executed in the LSM subsystem.

1. **Index scan**

    When an index is used, any additional columns needed for the query must be retrieved from the corresponding row in the main table, which can be more costly than scanning only the base table. However, this isn't an issue if the index is a covering index. To determine the most efficient execution plan, the CBO compares the cost of an index scan with that of a main table scan.

1. **Pushdown to storage layer**

    The CBO identifies possible operations that can be pushed down to the storage layer for aggregates, filters, and distinct clauses. This can considerably reduce network data transfer.

1. **Join strategies**

    For queries involving multiple tables, the CBO evaluates the cost of different join strategies like [Nested loop](../join-strategies/#nested-loop-join), [BNL](../join-strategies/#batched-nested-loop-join-bnl), [Merge](../join-strategies/#merge-join), or [Hash](../join-strategies/#hash-join) join, as well as various join orders.

1. **Data transfer**

    The CBO estimates the size and number of tuples that will be transferred, with data sent in pages. The page size is determined by the configuration parameters [yb_fetch_row_limit](../../../reference/configuration/yb-tserver/#yb-fetch-row-limit) and [yb_fetch_size_limit](../../../reference/configuration/yb-tserver/#yb-fetch-size-limit). Because each page requires a network round trip for the request and response, the CBO also estimates the total number of pages that will be transferred. Note that the time spent transferring the data also depends on the network bandwidth.

## Plan selection

The CBO evaluates each candidate plan's estimated costs to determine the plan with the lowest cost, which is then selected for execution. This ensures the optimal use of system resources and improved query performance.

After the optimal plan is determined, YugabyteDB generates a detailed execution plan with all the necessary steps, such as scanning tables, joining data, filtering rows, sorting, and computing expressions. This execution plan is then passed to the query executor component, which carries out the plan and returns the final query results.

## Plan caching

The execution plans are cached for prepared statements to avoid overheads associated with repeated parsing of statements.

## Learn more

- [Exploring the Cost Based Optimizer](https://www.yugabyte.com/blog/yugabytedb-cost-based-optimizer/)
- [YugabyteDB Cost-Based Optimizer](https://dev.to/yugabyte/yugabytedb-cost-based-optimizer-and-cost-model-for-distributed-lsm-tree-1hb4)