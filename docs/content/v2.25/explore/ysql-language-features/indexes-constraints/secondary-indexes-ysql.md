---
title: Secondary indexes in YugabyteDB YSQL
headerTitle: Secondary indexes
linkTitle: Secondary indexes
description: Overview of Secondary indexes in YSQL
headContent: Explore secondary indexes in YugabyteDB using YSQL
menu:
  v2.25:
    identifier: secondary-indexes-ysql
    parent: explore-indexes-constraints-ysql
    weight: 210
type: docs
---

Using indexes enhances database performance by enabling the database server to find rows faster. You can create, drop, and list indexes, as well as use indexes on expressions.

In YugabyteDB, indexes are global and are implemented just like tables. They are split into tablets and distributed across the different nodes in the cluster. Unless the index is colocated or copartitioned, the sharding of indexes is based on the primary key of the index and is independent of how the main table is sharded and distributed.

Indexes are created in the following format:

```sql{.nocopy}
CREATE INDEX idx_name ON table_name
   ((columns),     columns)    INCLUDE (columns)
--  [SHARDING]    [CLUSTERING]         [COVERING]
```

The columns that are specified in the [CREATE INDEX](../../../../api/ysql/the-sql-language/statements/ddl_create_index) statement are of three kinds:

- **Sharding** - Columns that determine how the index data is distributed.
- **Clustering** - Optional columns that determine how index rows that match the same sharding key are ordered.
- **Covering** - Optional columns that are stored in the index to avoid a trip to the table.

## Setup

{{% explore-setup-single-new %}}

Create a census table as follows:

```sql
CREATE TABLE census(
   id int,
   name varchar(255),
   age int,
   zipcode int,
   employed boolean,
   PRIMARY KEY(id ASC)
);
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

## Simple index

You can create indexes using [CREATE INDEX](../../../../api/ysql/the-sql-language/statements/ddl_create_index/) to speed up lookups on a singe column.

For example to create an index on zipcode, you can run:

```sql
CREATE INDEX idx_zip on census(zipcode);
```

## Multi-column index

You can create indexes on multiple columns, enabling queries with conditions on all the columns in the index to perform faster. For example, to speed up a query that looks up data based on name and zipcode:

```sql
SELECT * FROM census WHERE name = 'Kevin' AND zipcode = 94085;
```

Create an index on name and zipcode as follows:

```sql
CREATE INDEX idx_name_zip on census(name, zipcode);
```

## List indexes

YSQL inherits all the functionality of the PostgreSQL [pg_indexes](https://www.postgresql.org/docs/12/view-pg-indexes.html) view that allows you to retrieve a list of all indexes in the database as well as detailed information about every index.

```sql
SELECT indexname, indexdef FROM pg_indexes WHERE tablename = 'census';
```

## Remove indexes

You can remove one or more existing indexes using the [DROP INDEX](../../../../api/ysql/the-sql-language/statements/ddl_drop_index/) statement. For example, to drop the index on zipcode, you can run:

```sql
DROP INDEX idx_zip;
```

## Understanding performance

You can use the [EXPLAIN ANALYZE](../../../../api/ysql/the-sql-language/statements/perf_explain/) statement to check if a query uses an index and determine the query plan before execution. For example, consider the following query:

```sql
EXPLAIN (ANALYZE, DIST, COSTS OFF) SELECT * FROM census WHERE name = 'Kevin' AND zipcode = 94085;
```

Without the corresponding index, the query plan would be:

```yaml{.nocopy}
                                      QUERY PLAN
---------------------------------------------------------------------------------------
 Seq Scan on public.census (actual time=2.153..2.159 rows=1 loops=1)
   Output: id, name, age, zipcode, employed
   Remote Filter: (((census.name)::text = 'Kevin'::text) AND (census.zipcode = 94085))
   Storage Table Read Requests: 1
   Storage Table Read Execution Time: 1.888 ms
   Storage Table Rows Scanned: 45
```

All 45 rows in the table were scanned to retrieve one row. Now add an index for this query.

```sql
CREATE INDEX idx_name_zip on census(name, zipcode);
```

The query plan after the index is added would be something like:

```yaml{.nocopy}
                                    QUERY PLAN
-----------------------------------------------------------------------------------
 Index Scan using idx_name_zip on census (actual time=5.821..5.825 rows=1 loops=1)
   Index Cond: (((name)::text = 'Kevin'::text) AND (zipcode = 94085))
   Storage Table Read Requests: 1
   Storage Table Read Execution Time: 0.720 ms
   Storage Table Rows Scanned: 1
   Storage Index Read Requests: 1
   Storage Index Read Execution Time: 2.324 ms
   Storage Index Rows Scanned: 1
```

Now only one row is scanned to retrieve one row.

## Learn more

- [Designing secondary indexes](/preview/develop/data-modeling/secondary-indexes-ysql)
- [Benefits of Index-only scan](https://www.yugabyte.com/blog/how-a-distributed-sql-database-boosts-secondary-index-queries-with-index-only-scan/)
- Blog on [Pushdown #3: Filtering using index predicates](https://www.yugabyte.com/blog/5-query-pushdowns-for-distributed-sql-and-how-they-differ-from-a-traditional-rdbms/) discusses the performance boost of distributed SQL queries using indexes.
- [How To Design Distributed Indexes for Optimal Query Performance](https://www.yugabyte.com/blog/design-indexes-query-performance-distributed-database/)
