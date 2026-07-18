---
title: Query Planner
headerTitle: Query Planner
linkTitle: Query Planner
headcontent: Understand how the planner chooses the optimal path for query execution
menu:
  v2.20:
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
