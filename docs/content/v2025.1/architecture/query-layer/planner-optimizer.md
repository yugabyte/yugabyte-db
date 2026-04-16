---
title: Query Planner
headerTitle: Query Planner / CBO
linkTitle: Query Planner
headcontent: Understand how the planner chooses the optimal path for query execution
menu:
  v2025.1:
    identifier: query-planner
    parent: architecture-query-layer
    weight: 100
type: docs
rightnav:
  hideH4: true
---

The query planner is responsible for determining the most efficient way to execute a given query. The optimizer is the critical component in the planner that calculates the costs of different execution plans, taking into account factors like index lookups, table scans, network round trips, and storage costs. It then selects the most cost-effective path for query execution. YugabyteDB implements completely different types of optimizers for the YSQL and YCQL APIs.

## Rule based optimizer (YCQL)

YugabyteDB implements a simple rules-based optimizer (RBO) for YCQL. It operates by applying a predefined set of rules to optimize queries, such as reordering joins to minimize the number of rows processed, pushing selection conditions down the query tree, and using indexes and views to enhance performance.

## Heuristics based optimizer (YSQL)

YugabyteDB's YSQL API uses a simple heuristics-based optimizer to determine the most efficient execution plan for a query. It relies on basic statistics, like table sizes, and applies heuristics to estimate the cost of different plans. The cost model is based on PostgreSQL's approach, using data such as row counts and index availability, and assigns some heuristic costs to the number of result rows depending on the type of scan. Although this works well for most queries, because this model was designed for single-node databases like PostgreSQL, it doesn't account for YugabyteDB's distributed architecture or take cluster topology into consideration during query planning.

## Cost-based optimizer (YSQL)

To account for the distributed nature of the data, YugabyteDB has implemented a cost-based optimizer (CBO) for YSQL that uses an advanced cost model. The model considers accurate table statistics, the cost of network round trips, operations on lower level storage layer, and the cluster topology.

The YugabyteDB CBO is disabled by default.

For more information on configuring CBO, refer to [Enable cost-based optimizer](../../../best-practices-operations/ysql-yb-enable-cbo/).

### Plan search algorithm

To optimize the search for the best plan, the CBO uses a dynamic programming-based algorithm. Instead of enumerating and evaluating the cost of each possible execution plan, it breaks the problem down and finds the most optimal sub-plans for each part of the query. The sub-plans are then combined to find the best overall plan.

### Statistics gathering

The optimizer relies on accurate statistics about the tables, including the number of rows, the distribution of data in columns, and the cardinality of results from operations. These statistics are essential for estimating the selectivity of filters and costs of various query plans accurately. These statistics are gathered by the [ANALYZE](../../../api/ysql/the-sql-language/statements/cmd_analyze/) command and are provided in a display-friendly format by the [pg_stats](../../../architecture/system-catalog/#data-statistics) view.

Similar to [PostgreSQL autovacuum](https://www.postgresql.org/docs/current/routine-vacuuming.html#AUTOVACUUM), the YugabyteDB [Auto Analyze](../../../additional-features/auto-analyze/) service automates the execution of ANALYZE commands for any table where rows have changed more than a configurable threshold for the table. This ensures table statistics are always up-to-date.

Even with the Auto Analyze service, for the CBO to create optimal execution plans, you should still run ANALYZE manually on user tables after data load, as well as in other circumstances. Refer to [Best practices](#best-practices).

### Cost estimation

For each potential execution plan, the optimizer calculates costs in terms of storage layer lookups (both cache and disk), number of network round trips, and other factors. These costs help the optimizer compare which plan is likely be the most efficient to execute given the current database state and query context.

{{<tip>}}
You can see these estimates when using the DEBUG option in the [EXPLAIN](../../../api/ysql/the-sql-language/statements/perf_explain) command, as in EXPLAIN (ANALYZE, DEBUG).
{{</tip>}}

Some of the factors that the CBO considers in the cost estimation are as follows:

1. **Data fetch**

    To estimate the cost of fetching a tuple from [DocDB](../../docdb/), the CBO takes into account factors such as the number of SST files that may need to be read, and the estimated number of [seek](../../docdb/lsm-sst/#seek), [previous](../../docdb/lsm-sst/#previous), and [next](../../docdb/lsm-sst/#next) operations that may be executed in the LSM subsystem.

1. **Index scan**

    When an index is used, any additional columns needed for the query must be retrieved from the corresponding row in the main table, which can be more costly than scanning only the base table. However, this isn't an issue if the index is a covering index. To determine the most efficient execution plan, the CBO compares the cost of an index scan with that of a main table scan.

1. **Pushdown to storage layer**

    The CBO identifies possible operations that can be pushed down to the storage layer for aggregates, filters, and distinct clauses. This can considerably reduce network data transfer.

1. **Join strategies**

    For queries involving multiple tables, the CBO evaluates the cost of different join strategies like [nested loop](../join-strategies/#nested-loop-join), [batch nested loop](../join-strategies/#batched-nested-loop-join-bnl), [merge](../join-strategies/#merge-join), or [hash](../join-strategies/#hash-join) join, as well as various join orders.

1. **Data transfer**

    The CBO estimates the size and number of tuples that will be transferred, with data sent in pages. The page size is determined by the configuration parameters [yb_fetch_row_limit](../../../reference/configuration/yb-tserver/#yb-fetch-row-limit) and [yb_fetch_size_limit](../../../reference/configuration/yb-tserver/#yb-fetch-size-limit). Because each page requires a network round trip for the request and response, the CBO also estimates the total number of pages that will be transferred. Note that the time spent transferring the data also depends on the network bandwidth.

### Plan selection

The CBO evaluates each candidate plan's estimated costs to determine the plan with the lowest cost, which is then selected for execution. This ensures the optimal use of system resources and improved query performance.

After the optimal plan is determined, YugabyteDB generates a detailed execution plan with all the necessary steps, such as scanning tables, joining data, filtering rows, sorting, and computing expressions. This execution plan is then passed to the query executor component, which carries out the plan and returns the final query results.

### Best practices

- If your table already has rows, and you create an additional index (for example, `create index i on t (k);`), you must re-run analyze to populate the index `pg_class.reltuples` with the correct row count. {{<issue 25394>}}

    If you need to create a new index to replace a old one while your application is running, create the new one first, run analyze, then drop the old one.

- After you restore a database in YugabyteDB Anywhere or Aeon, you need to run analyze because the statistics that were in the database when it was backed up do not get restored.

    For consistent RTO from restore in a cost model-enabled environment, run analyze manually after restore has finished and before you allow end users into the application. If you allow end users into the application before analyze is finished, initial execution plans won't be correctly optimized.

## Learn more

- [Exploring the Cost Based Optimizer](https://www.yugabyte.com/blog/yugabytedb-cost-based-optimizer/)
- [YugabyteDB Cost Based Optimizer](https://dev.to/yugabyte/yugabytedb-cost-based-optimizer-and-cost-model-for-distributed-lsm-tree-1hb4)