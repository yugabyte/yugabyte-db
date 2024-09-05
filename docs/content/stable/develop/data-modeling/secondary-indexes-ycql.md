---
title: Designing secondary indexes in YugabyteDB
headertitle: Designing secondary indexes
linkTitle: Secondary indexes
badges: ycql
menu:
  stable:
    identifier: data-modeling-indexes-ycql
    parent: data-modeling
    weight: 200
type: docs
---

{{<api-tabs>}}

The primary goal of an index is to enhance the performance of data retrieval operations on the data in the tables. Indexes are designed to quickly locate data without having to search every row in a database table and provide fast access for patterns other than that of the primary key of the table. In YugabyteDB, indexes are internally designed just like tables and operate as such. The main difference between a table and an index is that the primary key of the table has to be unique but it need not be unique for an index.

{{<note>}}
In YugabyteDB, indexes are global and are implemented just like tables. They are split into tablets and distributed across the different nodes in the cluster. The sharding of indexes is based on the primary key of the index and is independent of how the main table is sharded and distributed. Indexes are not colocated with the base table.
{{</note>}}

To illustrate secondary indexes, first create a sample census schema.

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

Create a census table as follows:

```sql
create keyspace if not exists yugabyte;
use yugabyte;

drop table if exists census;
CREATE TABLE census(
   id int,
   name varchar,
   age int,
   zipcode int,
   employed boolean,
   PRIMARY KEY(id)
)  WITH transactions = { 'enabled' : true };
```

{{<warning>}}
To attach indexes to tables, the tables should be created with transactions enabled.
{{</warning>}}

<details> <summary>Add some data to the table as follows.</summary>

```sql
INSERT INTO census (id,name,age,zipcode,employed) VALUES (1,'Zachary',55,94085,True);
INSERT INTO census (id,name,age,zipcode,employed) VALUES (2,'James',56,94085,False);
INSERT INTO census (id,name,age,zipcode,employed) VALUES (3,'Kimberly',50,94084,False);
INSERT INTO census (id,name,age,zipcode,employed) VALUES (4,'Edward',56,94085,True);
INSERT INTO census (id,name,age,zipcode,employed) VALUES (5,'Barry',56,94084,False);
INSERT INTO census (id,name,age,zipcode,employed) VALUES (6,'Tyler',45,94084,False);
INSERT INTO census (id,name,age,zipcode,employed) VALUES (7,'James',47,94085,False);
INSERT INTO census (id,name,age,zipcode,employed) VALUES (8,'Sarah',52,94084,True);
INSERT INTO census (id,name,age,zipcode,employed) VALUES (9,'James',59,94084,False);
INSERT INTO census (id,name,age,zipcode,employed) VALUES (10,'Diane',51,94083,False);
```

</details>

## Basic index

Suppose you need to look up the data based on the zip codes of the people in the census. You can fetch details with a query similar to the following:

```sql
select id,name from census where zipcode=94085;
```

This required a sequential scan of all the rows in the table. This is because the primary key of the table is `id`, and looking up by zip code requires a full scan. To avoid the full scan, create an index on `zipcode` so that the executor can quickly fetch the matching rows by looking at the index.

```sql
create index idx_zip on census(zipcode);
```

Now, for a query to get all the people in zip code 94085 as follows:

```sql
explain select id,name from census where zipcode=94085;
```

You will see an output like the following:

```yaml{.nocopy}
 Index Scan using yugabyte.idx_zip on yugabyte.census
   Key Conditions: (zipcode = 94085)
```

The same 4 rows were fetched from the table, but much faster. This is because the planner uses the index to execute the query.

## Covering index

In the prior example, to retrieve the rows the index was first looked up, and then more columns (such as `name`) were fetched for the same rows from the table. This additional round trip to the table is needed because the columns are not present in the index. To avoid this, you can store the column along with the index as follows:

```sql
create index idx_zip2 on census(zipcode) include(name);
```

Now, for a query to get all people in zip code 94085 as follows:

```sql
explain select id,name from census where zipcode=94085;
```

You will see an output like the following:

```yaml{.nocopy}
 Index Only Scan using yugabyte.idx_zip2 on yugabyte.census
   Key Conditions: (zipcode = 94085)
```

This is an index-only scan, which means that all the data required by the query has been fetched from the index. This is also why there was no entry for Table Read Requests.

{{<tip>}}
When an index contains all the columns of the table, it is referred to as a Duplicate index.
{{</tip>}}

## Listing indexes

You can list the indexes associated with a table using the following methods.

### DESC command

The `DESC TABLE <table>` command lists the indexes associated with a table along with the schema details.

```cql
DESC TABLE census;
```

The indexes are listed at the end of the output as follows:

```cql{.nocopy}
CREATE TABLE yugabyte.census (
    id int PRIMARY KEY,
    name text,
    age int,
    zipcode int,
    employed boolean
) WITH default_time_to_live = 0
    AND transactions = {'enabled': 'true'};
CREATE INDEX idx_zip2 ON yugabyte.census (zipcode, id) INCLUDE (name)
    WITH transactions = {'enabled': 'true'};
CREATE INDEX idx_zip ON yugabyte.census (zipcode, id)
    WITH transactions = {'enabled': 'true'};
```

The `DESC INDEX <index-name>` command gives just the description of the specified index.

```cql
DESC INDEX idx_zip2;
```

The output includes the description of just the index as follows:

```cql{.nocopy}
CREATE INDEX idx_zip2 ON yugabyte.census (zipcode, id) INCLUDE (name)
    WITH transactions = {'enabled': 'true'};
```

## Conclusion

While primary keys are essential to ensure data uniqueness and facilitate efficient data distribution, secondary indexes provide the flexibility needed to optimize queries based on non-primary key columns. Using secondary indexes, applications can boost performance and provide a robust and scalable solution for managing large-scale, distributed datasets.

## Learn more

- [Explore indexes and constraints](../../../explore/ycql-language/indexes-constraints/)
