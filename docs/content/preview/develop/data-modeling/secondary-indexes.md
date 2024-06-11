---
title: Designing secondary indexes in YugabyteDB
headertitle: Designing secondary indexes
linkTitle: Secondary indexes
badges: ysql
menu:
  preview:
    identifier: data-modeling-indexes
    parent: data-modeling
    weight: 200
type: docs
---


The primary goal of an index is to enhance the performance of data retrieval operations on the data in the tables. Indexes are designed to quickly locate data without having to search every row in a database table and provide fast access for patterns other than that of the primary key of the table. In YugabyteDB, indexes are internally designed just like tables and operate as such. The main difference between a table and an index is that the primary key of the table has to be unique but it need not be unique for an index.

{{<note>}}
In YugabyteDB, indexes are global and are implemented just like tables. They are split into tablets and distributed across the different nodes in the cluster. The sharding of indexes is based on the primary key of the index and is independent of how the main table is sharded and distributed. Indexes are not colocated with the base table.
{{</note>}}

Let us understand indexes in details with a sample census schema.

<!-- begin: nav tabs -->
{{<nav/tabs list="local,anywhere" active="local" repeatedTabs="true"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- local cluster setup instructions -->
{{<setup/local>}}
{{</nav/panel>}}

{{<nav/panel name="anywhere">}} {{<setup/anywhere>}} {{</nav/panel>}}
{{</nav/panels>}}
<!-- end: nav tabs -->

For illustration, create a census table as follows.

```sql
CREATE TABLE census(
   id int,
   name varchar(255),
   age int,
   zipcode int,
   employed boolean,
   PRIMARY KEY(id ASC)
)
```

<details> <summary>Add some data to the table as follows.</summary>

```sql
INSERT INTO public.census ( id,name,age,zipcode,employed ) VALUES
  (1,'Zachary',55,94085,True),    (2,'James',56,94085,False),    (3,'Kimberly',50,94084,False),
  (4,'Edward',56,94085,True),     (5,'Barry',56,94084,False),    (6,'Tyler',45,94084,False),
  (7,'Nancy',47,94085,False),     (8,'Sarah',52,94084,True),     (9,'Nancy',59,94084,False),
  (10,'Diane',51,94083,False),    (11,'Ashley',42,94083,False),  (12,'Jacqueline',58,94085,False),
  (13,'Benjamin',49,94084,False), (14,'James',48,94083,False),   (15,'Ann',43,94083,False),
  (16,'Aimee',47,94085,True),     (17,'Michael',49,94085,False), (18,'Rebecca',40,94085,False),
  (19,'Kevin',45,94085,True),     (20,'James',45,94084,False),   (21,'Sandra',60,94085,False),
  (22,'Kathleen',40,94085,True),  (23,'William',42,94084,False), (24,'James',42,94083,False),
  (25,'Tyler',50,94085,False),    (26,'James',49,94085,True),    (27,'Kathleen',55,94083,True),
  (28,'Zachary',55,94083,True),   (29,'Rebecca',41,94085,True),  (30,'Jacqueline',49,94085,False),
  (31,'Diane',48,94083,False),    (32,'Sarah',53,94085,True),    (33,'Rebecca',55,94083,True),
  (34,'William',47,94085,False),  (35,'William',60,94085,True),  (36,'Sarah',53,94085,False),
  (37,'Ashley',47,94084,True),    (38,'Ashley',54,94084,False),  (39,'Benjamin',42,94083,False),
  (40,'Tyler',47,94085,True),     (41,'Michael',42,94084,False), (42,'Diane',50,94084,False),
  (43,'Nancy',51,94085,False),    (44,'Rebecca',56,94085,False), (45,'Tyler',41,94085,True);
```

</details>

## Basic index

Suppose you also need to look up the data based on the zip codes of the people in the census. You can fetch details with a query similar to the following:

```sql
select id from census where zipcode=94085;
```

You will quickly notice that this required a sequential scan of all the rows in the table. This is because the primary key of the table is `id`, and looking up by zip code requires a full scan. To avoid the full scan, you need to create an index on `zipcode` so that the executor can quickly fetch the matching rows by looking at the index.

```sql
create index idx_zip on census(zipcode ASC);
```

Now, for a query to get all the people in zip code 94085 as follows:

```sql
explain (analyze, dist, costs off) select id from census where zipcode=94085;
```

You will see an output like the following:

```yaml{.nocopy}
 Index Scan using idx_zip on public.census (actual time=3.273..3.295 rows=23 loops=1)
   Output: id
   Index Cond: (census.zipcode = 94085)
   Storage Table Read Requests: 1
   Storage Table Read Execution Time: 1.401 ms
   Storage Table Rows Scanned: 23
   Storage Index Read Requests: 1
   Storage Index Read Execution Time: 1.529 ms
   Storage Index Rows Scanned: 23
...
```

You will quickly notice that the same 23 rows were fetched from the table , but much faster. This is because, the planner uses the index to execute the query.

## Covering index

In the prior example, to retrieve 23 rows the index was first looked up and then more columns were fetched for the same rows from the table. This additional round trip to the table is needed because the columns are not present in the index. To avoid this, you can store the column along with the index as follows:

```sql
create index idx_zip2 on census(zipcode ASC) include(id);
```

Now, for a query to get all the people in zip code 94085 as follows:

```sql
explain (analyze, dist, costs off) select id from census where zipcode=94085;
```

You will see an output like the following:

```yaml{.nocopy}
                                     QUERY PLAN
-------------------------------------------------------------------------------------
 Index Only Scan using idx_zip2 on census (actual time=1.930..1.942 rows=23 loops=1)
   Index Cond: (zipcode = 94085)
   Storage Index Read Requests: 1
   Storage Index Read Execution Time: 1.042 ms
   Storage Index Rows Scanned: 23
...
```

This has become an index-only scan, which means that all the data required by the query has been fetched from the index. This is also why there was no entry for Table Read Requests.

As a special case, if the index contains all the columns of the table, then it is referred as Duplicate index. Duplicate indexes can be very useful especially in multi-region deployemnts to reduce read latencies.

{{<lead link="../../../develop/build-global-apps/duplicate-indexes/">}}
See [Duplicate indexes](../../../develop/build-global-apps/duplicate-indexes/) for more details
{{</lead>}}

## Listing indexes

You can list the indexes associated with a table using the following methods.

### \d+ meta command

The `\d+ <table>` meta command will list the indexes associated with a table along with the schema details.

The command

```sql
\d+ census
```

will give an output where the indexes are listed at the end of the output as below.

```yaml{.nocopy}
  Column  |          Type          | Collation | Nullable | Default | Storage  | Stats target | Description
----------+------------------------+-----------+----------+---------+----------+--------------+-------------
 id       | integer                |           | not null |         | plain    |              |
 name     | character varying(255) |           |          |         | extended |              |
 age      | integer                |           |          |         | plain    |              |
 zipcode  | integer                |           |          |         | plain    |              |
 employed | boolean                |           |          |         | plain    |              |
Indexes:
    "census_pkey" PRIMARY KEY, lsm (id ASC)
    "idx_zip" lsm (zipcode ASC)
```

### pg_indexes view

You can also fetch more information about indexes using the [pg_indexes](../../../architecture/system-catalog#schema) view.

```sql
SELECT * FROM pg_indexes WHERE tablename = 'census' ;
```

This would give an output similar to,

```yaml{.nocopy}
 schemaname | tablename |  indexname  | tablespace |                              indexdef
------------+-----------+-------------+------------+---------------------------------------------------------------------
 public     | census    | census_pkey | null       | CREATE UNIQUE INDEX census_pkey ON public.census USING lsm (id ASC)
 public     | census    | idx_zip     | null       | CREATE INDEX idx_zip ON public.census USING lsm (zipcode ASC)
```

## Index usage

It is a good idea to keep track of how well indexes are used by your applications. This will be very helpful to improve your existing indexes and drop indexes that are not useful. To get the usage statistics of the indexes of a table, you can execute the following command.

```sql
SELECT * FROM pg_stat_user_indexes WHERE relname = 'census';
```

This should give an output similar to,

```yaml{.nocopy}
 relid | indexrelid | schemaname | relname | indexrelname | idx_scan | idx_tup_read | idx_tup_fetch
-------+------------+------------+---------+--------------+----------+--------------+---------------
 17227 |      17230 | public     | census  | census_pkey  |        2 |           12 |             0
 17227 |      17237 | public     | census  | idx_zip      |        2 |           24 |             0
```

You can get an idea of how many times the index was scanned and how many tuples were read from the index using this statistic.

## Conclusion

While primary keys are essential for ensuring data uniqueness and facilitating efficient data distribution, secondary indexes provide the flexibility needed to optimize queries based on non-primary key columns. By strategically employing secondary indexes, applications can achieve a significant boost in performance, providing users with a robust and scalable solution for managing large-scale, distributed datasets

## Learn more

- [Use Explain Analyze to improve query performance](../../../explore/query-1-performance/explain-analyze)
- [Explore indexes and constraints](../../../explore/ysql-language-features/indexes-constraints/)