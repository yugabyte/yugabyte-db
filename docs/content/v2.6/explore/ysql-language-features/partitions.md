---
title: Table Partitioning
linkTitle: Table Partitioning
description: Table Partitioning in YSQL
headcontent: Table Partitioning in YSQL
image: /images/section_icons/secure/create-roles.png
menu:
  v2.6:
    identifier: explore-ysql-language-features-partitioins
    parent: explore-ysql-language-features
    weight: 315
isTocNested: true
showAsideToc: true
---

This section describes how to partition tables in YugabyteDB using YSQL.

## Overview

Partitioning is another term for physically dividing large tables in YugabyteDB into smaller, more manageable tables to improve performance. Typically, tables with columns containing timestamps are subject to partitioning because of the historical and predictable nature of their data.

Since partitioned tables do not appear nor act differently from the original table, applications accessing the database are not always aware of the fact that partitioning has taken place.

YSQL supports the following types of partitioning:

- Range partitioning, when a table is partitioned into ranges defined by one or more key columns. In this case, the ranges of values assigned to partitions do not overlap.
- List partitioning, when a table is partitioned via listing key values to appear in each partition.
- Hash partitioning, when a table is partitioned by specifying a modulus and remainder for each partition.

For supplementary information on partitioning, see [Row-Level Geo-Partitioning](../../multi-region-deployments/row-level-geo-partitioning/).

## Declarative Table Partitioning

YSQL allows you to specify how exactly to divide a table. You provide a partitioning method and partition key consisting of a list of columns or expressions. The divided table is called a partitioned table, and the resulting tables are called partitions. When you insert rows into a partitioned table, they are redirected to a partition depending on the value of the partition key. You can also directly insert rows into the partition table itself, and those rows can be fetched by querying the parent table.

You can nest partitions, in which case they would have their own distinct indexes, constraints, and default values.

A regular table cannot become a partitioned table, just as a partitioned table cannot become a regular table. That said, YSQL allows attaching a regular table (provided it has the same schema as that of the partitioned table) as a partition to the partitioned table. Conversely, a partition can be detached from the partitioned table, in which case it can behave as a regular table that is not part of the partitioning hierarchy. A partitioned table and its partitions have hierarchical structure and are subject to most of its rules.

Suppose you work with a database that includes the following table:

```sql
CREATE TABLE order_changes (
  change_date date,
  type text,
  description text
);
```

*change_date* represents the date when any type of change occurred on the order record. This date might be required when generating monthly reports. Assuming that typically only the last month's data is queried often, then the data older than one year is removed from the table every month. To simplify this process, you can partition the `order_changes` table. You start by specifying bounds corresponding to the partitioning method and partition key of the `order_changes` table. This means you create partitions as regular tables and YSQL generates partition constraints automatically based on the partition bound specification every time they have to be referenced.

You can declare partitioning on a table by creating it as a partitioned table: you specify the `PARTITION BY` clause which you supply with the partitioning method, such as `RANGE`, and a list of columns as a partition key, as shown in the following example:

```sql
CREATE TABLE order_changes (
  change_date date,
  type text,
  description text
)
PARTITION BY RANGE (change_date);
```

You create the actual partitions as follows:

```sql
CREATE TABLE order_changes_2019_02 PARTITION OF order_changes
  FOR VALUES FROM ('2019-02-01') TO ('2019-03-01');
```

```sql
CREATE TABLE order_changes_2019_03 PARTITION OF order_changes
  FOR VALUES FROM ('2019-03-01') TO ('2019-04-01');
```

...

```sql
CREATE TABLE order_changes_2020_11 PARTITION OF order_changes
  FOR VALUES FROM ('2020-11-01') TO ('2020-12-01');
```

```sql
CREATE TABLE order_changes_2020_12 PARTITION OF order_changes
  FOR VALUES FROM ('2020-12-01') TO ('2021-01-01');
```

```sql
CREATE TABLE order_changes_2021_01 PARTITION OF order_changes
  FOR VALUES FROM ('2021-01-01') TO ('2021-02-01');
```

Partitioning ranges are inclusive at the lower ( `FROM` ) bound and exclusive at the upper ( `TO` ) bound.
Each month range in the preceding examples includes the start of the month, but does not include the start of the following month.

To create a new partition that contains only the rows that don't match the specified partitions, add a default partition as follows:

```sql
CREATE TABLE order_changes_default PARTITION OF order_changes DEFAULT;
```

Optionally, you can create an index on the key columns and other indexes for every partition, as follows:

```sql
CREATE INDEX ON order_changes_2019_02 (change_date);
CREATE INDEX ON order_changes_2019_03 (change_date);
...
CREATE INDEX ON order_changes_2020_11 (change_date);
CREATE INDEX ON order_changes_2020_12 (change_date);
CREATE INDEX ON order_changes_2021_01 (change_date);
```

Partitioning is a flexible technique that allows you to remove old partitions and add new partitions for new data when required. You do this by changing the partition structure instead of the actual data.

The following example shows how to remove the partition from the partitioned table while retaining access to it as a separate table which enables you to perform operations on the data:

```sql
ALTER TABLE order_changes DETACH PARTITION order_changes_2019_03;
```

The following example shows how to add a new partition to deal with new data by creating an empty partition in the partitioned table:

```sql
CREATE TABLE order_changes_2021_02 PARTITION OF order_changes
  FOR VALUES FROM ('2021-02-01') TO ('2021-03-01');
```

Note the following:

- The primary key for a partitioned table should always contain the partition key. In the current release of YugabyteDB, it is recommended to set the primary key directly on partitions.
- If you choose to define row triggers, you do so on individual partitions instead of the partitioned table.
- A partition table does not inherit tablespaces from its parent. A partition table by default is placed according to cluster configuration.
- You cannot mix temporary and permanent relations in the same partition hierarchy.
- If you have a default partition in the partitioning hierarchy, you can add new partitions only if there is no data in the default partition that matches the partition constraint of the new partition.

## Partition Pruning

YSQL allows you to optimize queries ran on partitioned tables by eliminating (pruning) partitions that are no longer needed.

The following example shows a query that scans every partition of the `order_changes` table:

```sql
SELECT count(*) FROM order_changes WHERE change_date >= DATE '2020-01-01';
```

If you enable partition pruning on the preceding query by setting `enable_partition_pruning` to `on` (default), as shown in the following example, the query planner examines the definition of each partition and proves that the partition does not require scanning because it cannot contain data to satisfy the condition in the `WHERE` clause. This excludes the partition from the query plan.

```sql
SET enable_partition_pruning = on;
SELECT count(*) FROM order_changes WHERE change_date >= DATE '2020-01-01';
```

To disable partition pruning, set `enable_partition_pruning` to `off`.

## Constraint Exclusion

You can improve performance of partitioned tables by using a query optimization technique called constraint exclusion, as follows:

```sql
SET constraint_exclusion = on;
SELECT count(*) FROM order_changes WHERE change_date >= DATE '2021-01-01';
```

If you do not apply constraint exclusion, the preceding query would scan every partition of the `order_changes` table. If you add constraint exclusion, the query planner examines the constraints of each partition and attempts to prove that the partition does not require scanning because it cannot contain rows that meet the `WHERE` clause. If proven, the planner removes the partition from the query plan.

`constraint_exclusion` is not enabled (set to `on`) nor disabled (set to `off`) by default; instead, it is set to `partition`, therefore making constraint exclusion applied to queries that are executed on partitioned tables. When constraint exclusion is enabled, the planner examines `CHECK` constraints in all queries regardless of their complexity.

Note the following:

- You should only apply constraint exclusion to queries whose `WHERE` clause contains constants.
- For the query planner to be able to prove that partitions do not require visits, partitioning constraints need to be relatively simple.
- Since every constraint on every partition of the partitioned table is examined during constraint exclusion, having a lot of partitions significantly increases query planning time.
