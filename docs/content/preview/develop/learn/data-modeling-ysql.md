---
title: YSQL data modeling in YugabyteDB
headerTitle: Distributed SQL Data modeling
linkTitle: Data modeling
description: Learn data modeling in YSQL and how to identify the patterns used to access data and the types of queries to be performed.
menu:
  preview:
    identifier: data-modeling-ysql
    parent: learn
    weight: 100
type: docs
---

{{<api-tabs>}}

Data modeling involves designing the database schema for efficient storage and access. In a distributed SQL database like YugabyteDB, table data is split into tablets and distributed across multiple nodes within the cluster, allowing applications to connect to any node for storing and retrieving data. Reads and writes can span multiple nodes, so it's crucial to consider how table data is partitioned and distributed when modeling your data.

In YugabyteDB, data is stored as rows and columns in tables which are organized under schemas and databases.

{{<lead link="../../../explore/ysql-language-features/databases-schemas-tables">}}
To understand more about creating and managing tables, schemas, and databases, see [Schemas and tables](../../../explore/ysql-language-features/databases-schemas-tables).
{{</lead>}}

To design your tables and indexes for fast retrieval and storage in YugabyteDB, you first need to understand the two [data distribution](../../../explore/going-beyond-sql/data-sharding) schemes, Hash and Range sharding, in detail.

In YugabyteDB, the sharding and ordering of data in the tables and indexes is governed by the primary key of the table and index respectively.

## Cluster setup

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

## Sample data

For illustration, let us use a census table as follows.

```sql
CREATE TABLE census(
   id int,
   name varchar(255),
   age int,
   zipcode int,
   employed boolean,
   PRIMARY KEY(id ASC)
) SPLIT AT VALUES ((10), (25));
-- NOTE: We are explicitly splitting int 3 tablets for the purpose of illustration
-- In reality this is not needed, as tablets will auto-split
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

{{<note>}}
To explain the behavior of the queries, the examples use **explain (analyze, dist, costs off)**. In practice, you do not need to do this unless you are trying to optimize performance. For more details, see [Analyze queries](../../../explore/query-1-performance/explain-analyze).
{{</note>}}

## Primary keys

The Primary key is a column or a set of columns that uniquely identifies a row. The choice of primary key is very significant as it defines how data is distributed and stored ordered. It is best if it is chosen based on the most common access pattern. Typically suited for User ID, Order number etc. Columns of data types [string](../../../explore/ysql-language-features/data-types.md#strings), [number](../../../explore/ysql-language-features/data-types.md#numeric-types), [serial](../../../explore/ysql-language-features/data-types.md#serial-pseudotype), [UUID](../../../api/ysql/datatypes/type_uuid/) make good choices for primary keys.

Always specify the primary key when creating the table as it could be an expensive operation to define a primary key after the data has been added as the table data has to be re-ordered.

{{<warning>}}
In the absence of an explicit primary key, YugabyteDB automatically inserts an internal **row_id** to be used as the primary key. This **row_id** is not accessible by users.
{{</warning>}}

### ID as the primary key

In our `census` table, most likely we will be looking up person info based on their `id` so we have specified the primary key to be `id ASC`. This means that the data will be stored in the ascending order of id ensuring contiguous ids mostly be located in the same tablet. This will work great for point lookups on id and range scans on ids. For eg, to lookup `id=9`, you can do,

```sql
explain (analyze, dist, costs off) select * from census where id=9;
```

You will see an output similar to,

```yaml{.nocopy}
                                    QUERY PLAN
----------------------------------------------------------------------------------
 Index Scan using census_pkey on census (actual time=2.814..2.820 rows=1 loops=1)
   Index Cond: (id = 9)
   Storage Table Read Requests: 1
   Storage Table Read Execution Time: 1.560 ms
   Storage Table Rows Scanned: 1
...
```

One row matching `id=9` was fetched with just one request (`Storage Table Read Requests : 1`) as the system knows exactly where to look for that row. Also, only one row was scanned. But if you do a range scan for items across 2 tablets like:

```sql
explain (analyze, dist, costs off) select * from census where id>=9 and id<=10;
```

You will see an output similar to:

```yaml{.nocopy}
                                    QUERY PLAN
----------------------------------------------------------------------------------
 Index Scan using census_pkey on census (actual time=3.456..4.393 rows=11 loops=1)
   Index Cond: ((id >= 5) AND (id <= 15))
   Storage Table Read Requests: 2
   Storage Table Read Execution Time: 3.584 ms
   Storage Table Rows Scanned: 11
...
```

You will quickly notice that there are 2 `Table Read Requests`. This is because we have split the table at `id=10` and hence the rows with `id=[5,6,7,8,9]` are in one tablet and the rows with `id=[10,11,12,13,14,15]` are in another, so 2 requests had to be made.

### Name as the primary key

Let us say that your most common lookup is based on the name column. In such a case you need to have the name column as a part of the primary key. The `name` alone may not be good enough to be the primary key, as the primary key has to be unique. So you can choose a primary key with both `name` and `id` like,

```sql
CREATE TABLE census2(
   id int,
   name varchar(255),
   age int,
   zipcode int,
   employed boolean,
   PRIMARY KEY(name ASC, id ASC)
) SPLIT AT VALUES (('H'), ('S'));
-- NOTE: Splitting only for demo

-- copy the same data into census2
INSERT INTO census2 SELECT * FROM census;
```

Note that we have given the `name` column first and `id` second. This will ensure that the data is stored sorted based on `name` first and for all matching names, the `id` will be stored sorted in ascending order and all the people with the same name will be in the same tablet. This will also allow you to do a fast look up just by `name` even though `(name, id)` is the primary key. Now if you try to retrieve all the people info with `name=James`,

```sql
explain (analyze, dist, costs off) select * from census2 where name = 'James';
```

You will see an output similar to,

```yaml{.nocopy}
                                     QUERY PLAN
------------------------------------------------------------------------------------
 Index Scan using census2_pkey on census2 (actual time=1.489..1.496 rows=5 loops=1)
   Index Cond: ((name)::text = 'James'::text)
   Storage Table Read Requests: 1
   Storage Table Read Execution Time: 1.252 ms
   Storage Table Rows Scanned: 5
...
```

There are 5 people with `name=James`, but all of them are located in one tablet & that is why there is only one `Table Read Request`. If you do a range query like,

```sql
explain (analyze, dist, costs off) select * from census2 where name >= 'James' and name <='Michael';
```

You will see an output similar to,

```yaml{.nocopy}
                                      QUERY PLAN
---------------------------------------------------------------------------------------
 Index Scan using census2_pkey on census2 (actual time=2.411..2.430 rows=11 loops=1)
   Index Cond: (((name)::text >= 'James'::text) AND ((name)::text <= 'Michael'::text))
   Storage Table Read Requests: 1
   Storage Table Read Execution Time: 2.010 ms
   Storage Table Rows Scanned: 11
```

You will notice that there was only one `Table Read Request` to fetch the results as all the results with names - `James, Kathleen, Kevin, Kimberly, Michael` are all stored in the same tablet.

{{<note title="Ordering">}}
When defining the primary key we have specified `ASC` order, if the queries are going to retrieve data in descending order with `ORDER BY name DESC`, then it is better to match the same ordering in the primary key definition.
{{</note>}}

## Secondary indexes

The primary goal of an index is to enhance the performance of data retrieval operations on the data in the tables. Indexes are designed to quickly locate data without having to search every row in a database table and provide fast access patterns other than the primary key of the table. In YugabyteDB indexes are internally designed just like tables and operate as such. The primary difference between a table and an index is that the primary key of the table has to be unique but it need not be unique for an index.

Indexes can be created using the [CREATE INDEX](../../../api/ysql/the-sql-language/statements/ddl_create_index) statement which is of the format:

```sql{.nocopy}
CREATE INDEX idx_name ON table_name
   ((columns),     columns)    INCLUDE (columns)
--  [SHARDING]    [CLUSTERING]         [COVERING]
```

The columns that are specified in the [CREATE INDEX](../../../api/ysql/the-sql-language/statements/ddl_create_index) statement are of three kinds.

- **Sharding** : These columns determine how the index data is distributed.
- **Clustering** : These optional columns determine how the index rows matching the same sharding key are ordered.
- **Covering** : These are optional additional columns that are stored in the index to avoid a trip to the table.

### Basic index

If you also need to look up the data based on the zipcodes of the people in the census, you can fetch details with a query similar to,

```sql
explain (analyze, dist, costs off) select id from census where zipcode=94085;
```

For which you will get a query plan similar to,

```yaml{.nocopy}
                              QUERY PLAN
----------------------------------------------------------------------
 Seq Scan on public.census (actual time=4.201..4.206 rows=23 loops=1)
   Output: id
   Remote Filter: (census.zipcode = 94085)
   Storage Table Read Requests: 1
   Storage Table Read Execution Time: 1.928 ms
   Storage Table Rows Scanned: 45
```

You will quickly notice that this was a sequential scan of all the rows in the table. This is because the primary key of the table is either `name` or `id` and looking up by zipcode becomes a full scan operation. To avoid the full scan, you need to create an index on `zipcode` so that the executor can quickly fetch the matching rows by looking at the index.

```sql
create index idx_zip on census(zipcode ASC);
```

Now, for a query to get all the people in zipcode=94085 like:

```sql
explain (analyze, dist, costs off) select id from census where zipcode=94085;
```

You will see an output like,

```yaml{.nocopy}
                                        QUERY PLAN
-------------------------------------------------------------------------------------------
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

The index was used to identify all rows matching `zipcode = 94085`. 23 rows were fetched from the index and the corresponding data for the 23 rows were fetched from the table.

### Covering index

In the prior example we noticed that to retrieve 23 rows, the index was first looked up and then more columns were fetched for the same rows from the table. This additional round trip to the table is needed because the needed columns are not present in the index. We can avoid this by storing this column along with the index like,

```sql
create index idx_zip2 on census(zipcode ASC) include(id);
```

Now, for a query to get all the people in zipcode=94085 like:

```sql
explain (analyze, dist, costs off) select id from census where zipcode=94085;
```

You will see an output like,

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

You will notice that this has become an `index-only scan`, which means that all the data required by the query has been fetched just from the index. This is also why there was no entry for `Table Read Requests` as it was `0`.

## Hot shard or tablet

This is a common problem in data retrieval where a specific node ends up handling most of the queries because of the query pattern and data distribution scheme.

{{<warning>}}
 The hot shard issue can occur both for tables and indexes.
{{</warning>}}

Consider a scenario where you want to look up people with a specific name say `Michael` in `94085`. For this, a good index would be,

```sql
create index idx_zip3 on census(zipcode ASC, name ASC) include(id);
```

The query would look like,

```sql
explain (analyze, dist, costs off)  select id from census where zipcode=94085 AND name='Michael';
```

And you will get an output similar to,


```yaml{.nocopy}
                                      QUERY PLAN
------------------------------------------------------------------------------------
 Index Only Scan using idx_zip3 on census (actual time=1.618..1.620 rows=1 loops=1)
   Index Cond: ((zipcode = 94085) AND (name = 'Michael'::text))
   Heap Fetches: 0
   Storage Index Read Requests: 1
   Storage Index Read Execution Time: 0.970 ms
   Storage Index Rows Scanned: 1
```

Now, consider a scenario where zipcode `94085` is very popular and many queries are targeted at that zipcode (say an election or a disaster in that area). As the index is distributed based on `zipcode`, all folks in a specific zipcode (i.e. `94085``) will end up located in the same tablet & hence all the queries will end up reading from that tablet. Now, this tablet has become a hot tablet. We can avoid this by distributing the index on name instead of zipcode as,

```sql
drop index if exists idx_zip3;
create index idx_zip3 on census(name ASC, zipcode ASC) include(id);
```

Notice that we have swapped the order of columns in the index. This results in the index being distributed/ordered on name first and then ordered on zipcode. Now when many queries have the same zipcode, the queries will be handled by different tablets as the names being looked up will be different and will be located on different tablets.

{{<tip title="Remember">}}
Consider swapping the order of columns to avoid the Hot shard issue.
{{</tip>}}

## Partitioning

[Data partitioning](../../../explore/ysql-language-features/advanced-features/partitions) refers to the process of dividing a large table or dataset into smaller physical partitions based on certain criteria or rules. This technique offers several benefits, including improved performance, easier data management, and better utilization of storage resources. Each partition is internally a table. This scheme is useful for managing large volumes of data and especially useful for dropping older data.

### Management of large data

You can manage large data volumes by partitioning based on time, say by day, week, month etc for easier dropping of old data especially when you want to retain only the recent data.

{{<lead link="../../common-patterns/timeseries/partitioning-by-time">}}
To understand how large data can be partitioned for easier management, see [Partitioning data by time](../../common-patterns/timeseries/partitioning-by-time)
{{</lead>}}

### Placing data closer to users

When you want to improve latency for local users when your users are spread across a large geography, you partition your data according to where big clusters of users are located in and place their data in regions closer to them using [tablespaces](../../../explore/going-beyond-sql/tablespaces). Users will end up talking to partitions closer to them.

{{<lead link="../../build-global-apps/latency-optimized-geo-partition">}}
To understand how to partition and place them closer to user for improved latency, see [Latency-optimized geo-partitioning](../../build-global-apps/latency-optimized-geo-partition)
{{</lead>}}

### Adhering to compliance laws

You can partition your data according to the user's citizenship and place their data within the boundaries of their respective nations to be compliant with data residency laws like [GDPR](https://en.wikipedia.org/wiki/General_Data_Protection_Regulation).

{{<lead link="../../build-global-apps/locality-optimized-geo-partition">}}
To understand how to partition data to be compliant with data residency laws, see [Locality-optimized geo-partitioning](../../build-global-apps/locality-optimized-geo-partition)
{{</lead>}}