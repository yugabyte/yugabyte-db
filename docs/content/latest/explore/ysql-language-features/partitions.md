---
title: Table Partitioning
linkTitle: Table Partitioning
description: Table Partitioning in YSQL
headcontent: Table Partitioning in YSQL
image: /images/section_icons/secure/create-roles.png
menu:
  latest:
    identifier: explore-ysql-language-features-partitioins
    parent: explore-ysql-language-features
    weight: 300
isTocNested: true
showAsideToc: true
---

This section describes how to partition tables in YugabyteDB using YSQL.

## Overview

Partitioning is another term for physically dividing large tables in YugabyteDB into smaller, more manageable tables to improve performance. Typically, tables whose size exceeds the physical memory of the database server should be partitioned. With regards to the columns, the ones containing timestamps are usually subject to partitioning because of the historical and predictable nature of their data.

Since partitioned tables do not appear nor act differently from the original table, applications accessing the database are not always aware of the fact that partitioning has taken place.

It is recommended to use partitioned tables with a query planner that can decide which tables to access and which ones to ignore based on the table content. 

YSQL supports the following types of partitioning:

- Range partitioning, when a table is partitioned into ranges defined by one or more key columns. In this case, the ranges of values assigned to partitions do not overlap.
- List partitioning, when a table is partitioned via listing key values to appear in each partition.

## Declarative Table Partitioning

YSQL allows you to specify how exactly to divide a table. You provide a partitioning method and partition key consiting of a list of columns or expressions. The devided table is called a partitioned table, and the resulting tables are called partitions. When you insert rows into a partitioned table, they are redirected to a partition depending on the value of the partition key.

You can partition partitions, in which case they woud have their own distinct indexes, constraints, and default values.

A regular table cannot become a partitioned table, just as a partitioned table cannot become a regular table. That said, you can add a regular or partitioned table that has data as a partition of a partitioned table, and you can remove a partition from a partitioned table making it a standalone table.

A partitioned table and its partitions have hierarchical structure and are subject to its rules, except the following:

- If there are no partitions, you can use the `ONLY` clause to add or drop a constraint on the partitioned table. If there are partitions, you cannot use `ONLY` because adding or deleting constraints on the  partitioned tableis not supported in such cases.
- Partitions cannot have columns which do not exist in their partitioned table. You can add a partition using `ALTER TABLE ... ATTACH PARTITION` if their columns match the partitioned table, including a `oid` column.
- Partitions inherit `CHECK` and `NOT NULL` constraints of their partitioned table. This does not include `CHECK` constraints marked `NO INHERIT`.
- The `NOT NULL` constraint cannot be added to a partition's column if the constraint is present in their partitioned table.

Suppose you work with a database that includes the following table:

```sql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  department text,
  change_date date
);
```

*change_date* represents the date when any type of change occured on the employee record. This date is typically required when generating monthly reports and only the last month's data is queried. To reduce the amount of old data that  needs to be stored, You can reduce the amount of data to store and keep these data for one year, and on the first of every month you remove the data from the previous month. You can partition the `employees` table to meet these requirements. You start by specifying bounds corresponding to the partitioning method and partition key of the `employees` table. This means you create partitions as regular tables and YSQL generates partition constraints automatically based on the partition bound specification every time they have to be referenced.

You can declare partioning on the `employees` table by first creating it as a partitioned table. You specify the `PARTITION BY` clause and supply it with the partitioning method such as `RANGE`  as well as a list of columns as a partition key, as shown in the following example:

```sql
CREATE TABLE employees (
  employee_no integer PRIMARY KEY,
  name text,
  department text,
  change_date date
) 
PARTITION BY RANGE (change_date);
```

You create the actual partitions as follows:

```sql
CREATE TABLE employees_2019_02 PARTITION OF employees
  FOR VALUES FROM ('2019-02-01') TO ('2019-03-01');

CREATE TABLE employees_2019_03 PARTITION OF employees
  FOR VALUES FROM ('2019-03-01') TO ('2019-04-01');

...
CREATE TABLE employees_2020_11 PARTITION OF employees
  FOR VALUES FROM ('2020-11-01') TO ('2020-12-01');

CREATE TABLE employees_2020_12 PARTITION OF employees
  FOR VALUES FROM ('2020-12-01') TO ('2021-01-01')
  TABLESPACE fasttablespace;

CREATE TABLE employees_2021_01 PARTITION OF employees
  FOR VALUES FROM ('2021-01-01') TO ('2021-02-01')
  TABLESPACE fasttablespace;
```

Optionally, you can create an index on the key columns and other indexes for every partition, as follows:

```sql
CREATE INDEX ON employees_2019_02 (change_date);
CREATE INDEX ON employees_2019_03 (change_date);
...
CREATE INDEX ON employees_2020_11 (change_date);
CREATE INDEX ON employees_2020_12 (change_date);
CREATE INDEX ON employees_2021_01 (change_date);
```

Partitioning is a flexible technique that allows you to remove old partitions and add new partitions for  new data when required. You do this by changing the partition structure instead of the actual data.

The following example shows how to quickly remove old data via dropping the partition (you need to take an `ACCESS EXCLUSIVE` lock on the partitioned `employees` table):

```sql
DROP TABLE employees_2019_02;
```

The following example shows how remove the partition from the partitioned table while retaining access to it as a separate table which enables you to performed operatioins on the data:

```sql
ALTER TABLE employees DETACH PARTITION employees_2019_02;
```

The following example shows how to add a new partition to deal with new data by creating an empty partition in the partitioned table:

```sql
CREATE TABLE employees_2021_02 PARTITION OF employees
  FOR VALUES FROM ('2021-02-01') TO ('2021-03-01')
  TABLESPACE fasttablespace;
```

If your goal is to have data loaded, verified, and modified before it is added to the partitioned table, you can create a new table outside of the partition structure and then make it a partition when required: 

```sql
CREATE TABLE employees_2021_02
  (LIKE employees INCLUDING DEFAULTS INCLUDING CONSTRAINTS)
  TABLESPACE fasttablespace;

ALTER TABLE employees_2021_02 ADD CONSTRAINT 2021_02
  CHECK ( change_date >= DATE '2021-02-01' AND change_date < DATE '2021-03-01' );
...
ALTER TABLE employees ATTACH PARTITION employees_2021_02
  FOR VALUES FROM ('2021-02-01') TO ('2021-03-01');
```

Note the following:

- If you choose to define row triggers, you do so on individual partitions instead of the partitioned table.
- You cannot mix temporary and permanent relations in the same partition hierarchy.

## Table Partitioning Via Inheritance

In most cases, you use declarative table partitioning. However, if the following is required, you need to use partitioning via table inheritance:

- You need partitions to have more columns than the partitioned table.
- Multiple inheritance.
- You need data to be split based on the user's choice.
- You need to be able to add or remove a partition from a partitioned table using a `SHARE UPDATE EXCLUSIVE` lock instead of an `ACCESS EXCLUSIVE` lock on the partitioned table, as is the case with declarative partitioning.

The following example shows how to create child tables (partitions), each of which inherits from the `employees` table without adding any columns to the set inherited from the partitioned table (parent): 

```sql
CREATE TABLE employees_2019_02 () INHERITS (employees);
CREATE TABLE employees_2019_03 () INHERITS (employees);
...
CREATE TABLE employees_2020_11 () INHERITS (employees);
CREATE TABLE employees_2020_12 () INHERITS (employees);
CREATE TABLE employees_2021_01 () INHERITS (employees);
```

The next step is to add non-overlapping table constraints to the partition tables to define the key values in each partition, as shown in the following example:

```sql
CHECK ( n = 1 )
CHECK ( department IN ( 'Sales', 'Marketing'))
```

The following example shows how to create partitions:

```sql
CREATE TABLE employees_2019_02 (
  CHECK ( change_date >= DATE '2019-02-01' AND change_date < DATE '2019-03-01' )
) INHERITS (employees);

CREATE TABLE measurement_2019_03 (
  CHECK ( change_date >= DATE '2019-03-01' AND change_date < DATE '2019-04-01' )
) INHERITS (employees);
...
CREATE TABLE measurement_2020_11 (
  CHECK ( change_date >= DATE '2020-11-01' AND change_date < DATE '2020-12-01' )
) INHERITS (employees);

CREATE TABLE measurement_2020_12 (
  CHECK ( change_date >= DATE '2020-12-01' AND change_date < DATE '2020-01-01' )
) INHERITS (employees);

CREATE TABLE measurement_2021_01 (
  CHECK ( change_date >= DATE '2021-01-01' AND change_date < DATE '2021-02-01' )
) INHERITS (employees);
```

The following example shows how to create an index on the key columns and, optionally, other indexes on partitions:

```sql
CREATE INDEX employees_2019_02 ON employees_2019_02 (change_date);
CREATE INDEX employees_2019_03 ON employees_2019_03 (change_date);
CREATE INDEX employees_2020_11 ON employees_2020_11 (change_date);
CREATE INDEX employees_2020_12 ON employees_2020_12 (change_date);
CREATE INDEX employees_2021_01 ON employees_2021_01 (change_date);
```

The following example shows how to quickly remove old data via dropping the partition:

```sql
DROP TABLE employees_2019_02;
```

The following example shows how remove the partition from the partitioned table while retaining access to it as a separate table:

```sql
ALTER TABLE employees_2019_02 NO INHERIT employees;
```

The following example shows how to add a new partition to deal with new data by creating an empty partition:

```sql
CREATE TABLE employees_2021_02
  CHECK ( change_date >= DATE '2021-02-01' AND change_date < DATE '2021-03-01')
  TABLESPACE fasttablespace;
```

If your goal is to have data loaded, verified, and modified before it is added to the partitioned table, you can create a new table outside of the partition structure: 

```sql
CREATE TABLE employees_2021_02
  (LIKE employees INCLUDING DEFAULTS INCLUDING CONSTRAINTS);
ALTER TABLE employees_2021_02 ADD CONSTRAINT 2021_02
  CHECK ( change_date >= DATE '2021-02-01' AND change_date < DATE '2021-03-01' );
...
ALTER TABLE employees_2021_02 INHERIT employees;
```

Note the following:

- When using  `VACUUM` or `ANALYZE` commands, you have to run them on each partition. 
- Triggers or rules will be needed To direct rows to the desired partition, you need to use rules or triggers. 

## Constraint Exclusion

You can improve performance of partitioned tables by using a query optimization technique callled constraint exclusion, as follows:

```sql
SET constraint_exclusion = on;
SELECT count(*) FROM employees WHERE change_date >= DATE '2021-01-01';
```

If you do not apply constraint exclusion, the preceding query would scan every partition of the `employees` table. If you add constraint exclusion, the query planner examines the constraints of each partition and attempts to prove that the partition does not require scanning because it cannot contain rows that meet the `WHERE` clause. If proven, the planner removes the partition from the query plan.

`constraint_exclusion` is not enabled (set to `on`) nor disabled (set to `off`) by default; instead, it is set to `partition`, therefore making constraint exclusion applied to queries that are executed on partitioned tables. When constraint exclusion is enabled, the planner examines `CHECK` constraints in all queries regardless of their complexity.

Note the following:

- You should only apply constraint exclusion to queries whose  `WHERE` clause contains constants.
- For the query planner to be able to prove that partitions do not require visits, partitioning constraints need to be relatively simple.
- Since every constraint on every partition of the partitioned table is examined during constraint exclusion, having a lot of partitions significantly increase query planning time.