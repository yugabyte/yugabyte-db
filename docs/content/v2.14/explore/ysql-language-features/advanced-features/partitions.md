---
title: Table Partitioning
linkTitle: Table Partitioning
description: Table Partitioning in YSQL
image: /images/section_icons/secure/create-roles.png
menu:
  v2.14:
    identifier: advanced-features-partitions
    parent: advanced-features
    weight: 225
type: docs
---

This section describes how to partition tables in YugabyteDB using YSQL.

## Overview

Partitioning is another term for physically dividing large tables in YugabyteDB into smaller, more manageable tables to improve performance. Typically, tables with columns containing timestamps are subject to partitioning because of the historical and predictable nature of their data.

Since partitioned tables do not appear nor act differently from the original table, applications accessing the database are not always aware of the fact that partitioning has taken place.

YSQL supports the following types of partitioning:

- Range partitioning, when a table is partitioned into ranges defined by one or more key columns. In this case, the ranges of values assigned to partitions do not overlap.
- List partitioning, when a table is partitioned via listing key values to appear in each partition.
- Hash partitioning, when a table is partitioned by specifying a modulus and remainder for each partition.

For supplementary information on partitioning, see [Row-Level Geo-Partitioning](../../../multi-region-deployments/row-level-geo-partitioning/).

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

Optionally, you can create indexes on a partitioned table as follows:

```sql
yugabyte=# CREATE INDEX ON order_changes (change_date);
```

This automatically creates indexes on each partition, as demonstrated by the following output:

```output
yugabyte=# \d order_changes_2019_02
        Table "public.order_changes_2019_02"
   Column    | Type | Collation | Nullable | Default
-------------+------+-----------+----------+---------
 change_date | date |           |          |
 type        | text |           |          |
 description | text |           |          |
Partition of: order_changes FOR VALUES FROM ('2019-02-01') TO ('2019-03-01')
Indexes:
    "order_changes_2019_02_change_date_idx" lsm (change_date HASH)

...

yugabyte=# \d order_changes_2021_01
        Table "public.order_changes_2021_01"
   Column    | Type | Collation | Nullable | Default
-------------+------+-----------+----------+---------
 change_date | date |           |          |
 type        | text |           |          |
 description | text |           |          |
Partition of: order_changes FOR VALUES FROM ('2021-01-01') TO ('2021-02-01')
Indexes:
    "order_changes_2021_01_change_date_idx" lsm (change_date HASH)
```

Otherwise, you can create an index on the key columns and other indexes for every partition, as follows:

```sql
CREATE INDEX ON order_changes_2019_02 (change_date);
CREATE INDEX ON order_changes_2019_03 (change_date);
...
CREATE INDEX ON order_changes_2020_11 (change_date);
CREATE INDEX ON order_changes_2020_12 (change_date);
CREATE INDEX ON order_changes_2021_01 (change_date);
```

For the implications of creating an index on a partitioned table as opposed to creating indexes separately on each partition, see [CREATE INDEX](../../../../api/ysql/the-sql-language/statements/ddl_create_index/).

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

- The primary key for a partitioned table should always contain the partition key.
- If you choose to define row triggers, you do so on individual partitions instead of the partitioned table.
- Creating a foreign key reference on a partitioned table is not supported.
- A partition table inherits tablespaces from its parent.
- You cannot mix temporary and permanent relations in the same partition hierarchy.
- If you have a default partition in the partitioning hierarchy, you can add new partitions only if there is no data in the default partition that matches the partition constraint of the new partition.

## Partition pruning and constraint exclusion

Partition pruning and constraint exclusion are optimization techniques that allow the query planner to exclude unnecessary partitions from the execution. For example, consider the following query:

```sql
SELECT count(*) FROM order_changes WHERE change_date >= DATE '2020-01-01';
```

If the `order_changes` table is partitioned by `change_date`, there is a big chance that only a subset of partitions needs to be queried. When enabled, both partition pruning and constraint exclusion can provide significant performance improvements for such queries by filtering out partitions that do not satisfy the criteria.

Even though partition pruning and constraint exclusion target the same goal, the underlying mechanisms are different. Specifically, constraint exclusion is applied during query planning, and therefore only works if the `WHERE` clause contains constants or externally supplied parameters. For example, a comparison against a non-immutable function such as `CURRENT_TIMESTAMP` cannot be optimized, since the planner cannot know which child table the function's value might fall into at run time. On the other hand, partition pruning is applied during query execution, and therefore can be more flexible. However, it is only used for `SELECT` queries. Updates can only benefit from constraint exclusion.

Both optimizations are enabled by default, which is the recommended setting for the majority of cases. However, if you know for certain that one of your queries will have to scan all the partitions, you can consider disabling the optimizations for that query:

```sql
SET enable_partition_pruning = off;
SET constraint_exclusion = off;
SELECT count(*) FROM order_changes WHERE change_date >= DATE '2019-01-01';
```

To re-enable partition pruning, set the `enable_partition_pruning` setting to `on`.

For constraint exclusion, the recommended (and default) setting is neither `off` nor `on`, but rather an intermediate value `partition`, which means that it’s applied only to queries that are executed on partitioned tables.
