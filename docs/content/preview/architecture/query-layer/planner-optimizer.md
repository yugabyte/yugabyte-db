---
title: Query Planner
headerTitle: Query Planner / CBO
linkTitle: Query Planner
description: Understand the various methodologies used for joining multiple tables
headcontent: Understand how the planner choses the optimal path for query execution
menu:
  preview:
    identifier: query-planner
    parent: architecture-query-layer
    weight: 100
type: docs
---

The query planner is responsible for determining the most efficient way to execute a given SQL query. It generates various plans of exection and determines the optimal path by taking into consideration the costs associated various factors like index lookups, scans, CPU usage, network latency, and so on. The primary component that calculates these values is the Cost Based optimizer (CBO).

{{<note>}}
The Cost-based optimizer is a [YSQL](../../../api/ysql/) only feature.
{{</note>}}

{{<tip title="Enabling CBO">}}
The CBO is disabled by default. To enable it, turn ON the [yb_enable_base_scans_cost_model](../../../reference/configuration/yb-tserver/#yb-enable-base-scans-cost-model) flag as:

```sql
-- Enable for current session
SET yb_enable_base_scans_cost_model = TRUE;

-- Enable for all new sessions of a user
ALTER USER user SET yb_enable_base_scans_cost_model = TRUE;

-- Enable for all new sessions on a database
ALTER DATABASE database SET yb_enable_base_scans_cost_model = TRUE;
```

{{</tip>}}

Let us understand how this works.

## Plan search algorithm

To optimize the search for the best plan, CBO uses a dynamic programming-based algorithm. Instead of enumerating and evaluating the cost of each possible execution plan, it breaks the problem down and finds the most optimal sub-plans for each piece of the query. The sub-plans are then combined to find the best overall plan.

## Statistics gathering

The optimizer relies on accurate statistics about the tables, including the number of rows, the distribution of data within columns, and the cardinality of results from operations. These statistics are essential in estimating the costs of various query plans accurately. These statistics are gathered by the [ANALYZE](../../../api/ysql/the-sql-language/statements/cmd_analyze/) command and are provided in a display friendly format by the [pg_stats](../../../architecture/system-catalog/#data-statistics) view.

{{<note>}}
Currently the [ANALYZE](../../../api/ysql/the-sql-language/statements/cmd_analyze/) command has to be triggered manually. Multiple projects are in progress to  trigger this automatically.
{{</note>}}

## Cost estimation

For each potential execution plan, the optimizer calculates costs in terms of I/O, CPU usage, and memory consumption. These costs help the optimizer pragmatically compare which plan would likely be the most efficient to execute given the current database state and query context. Some of the factors included in the cost estimation are:

{{<tip>}}
These estimates can be seen when using the `DEBUG` option in the [EXPLAIN](../../../api/ysql/the-sql-language/statements/perf_explain) command as `EXPLAIN (ANALYZE, DEBUG)`.
{{</tip>}}

### Cost of data fetch

To estimate the cost of fetching a tuple from [DocDB](../../docdb/), factors such as the number of SST files that may need to be read, and the estimated number of [seeks](../../docdb/lsm-sst/#seek), [previous](../../docdb/lsm-sst/#previous), and [next](../../docdb/lsm-sst/#next) operations that may be executed within the LSM subsystem, are taken into account.

### Index scan

As the primary key is part of the base table and that each [SST](../../docdb/lsm-sst) of the base table is sorted in the order of the primary key the primary index lookup cheaper compared to secondary index lookup. Depending on the type of query this distintion is conidered.

### Pushdown to storage layer

CBO identifies possible operations that can be pushed down to the storage layer for aggregates, filters and distinct clauses. This can considerably reduce the data transer over network.

### Join strategies

For queries involving multiple tables the cost of different join strategies like [Nested loop](../join-strategies/#nested-loop-join), [BNL](../join-strategies/#batched-nested-loop-join-bnl), [Merge](../join-strategies/#merge-join) or [Hash](../join-strategies/#hash-join) join and various join orders are evaluated.

### Data transfer costs

The CBO estimates the size and number of tuples that will be transferred, with data sent in pages. The page size is determined by the configuration parameters [yb_fetch_row_limit](../../../reference/configuration/yb-tserver/#yb-fetch-row-limit) and [yb_fetch_size_limit](../../../reference/configuration/yb-tserver/#yb-fetch-size-limit). Since each page requires a network round trip for the request and response, the CBO also estimates the total number of pages that will be transferred. _Note_ that the time spent transferring the data will also depend on the network bandwidth.

## Plan selection

The CBO evaluates each candidate plan's estimated costs to determine the plan with the lowest cost, which is then selected for execution. This ensures the optimal usage of system resources and improved query performance.

After the optimal plan is determined, YugabyteDB generates a detailed execution plan with all the necessary steps, such as scanning tables, joining data, filtering rows, sorting, and computing expressions. This execution plan is then passed to the query executor component, which carries out the plan and returns the final query results.

## Plan caching

The execution plans are cached for prepared statements to avoid overheads associated with repeated parsing of statements.

## Learn more

- [Exploring the Cost Based Optimizer](https://www.yugabyte.com/blog/yugabytedb-cost-based-optimizer/)