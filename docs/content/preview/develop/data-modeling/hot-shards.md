---
title: Avoiding hotspots in YugabyteDB
headertitle: Avoiding hotspots
linkTitle: Hot shards
menu:
  preview:
    identifier: data-modeling-hot-shard
    parent: data-modeling
    weight: 300
type: docs
---

A hot shard is a common problem in data retrieval where a specific node becomes a performance bottleneck due to disproportionately high traffic or workload compared to other nodes in the system. This imbalance can lead to various issues, such as degraded system performance, increased latency, and potential system failures.

This typically happens because of mismatches between query pattern and data distribution. You should be careful when choosing a primary key in the schema design to not accidentally create hotspots in your database.

{{<warning>}}
The hot shard issue can occur both for tables and indexes.
{{</warning>}}

Let us understand the problem and the solution to this via some exmaples.

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

## Ordering of columns

Consider a scenario where you want to look up people with a specific name, say `Michael`, in `94085`. For this, a good index would be the following:

```sql
create index idx_zip3 on census(zipcode ASC, name ASC) include(id);
```

The query would be as follows:

```sql
select id from census where zipcode=94085 AND name='Michael';
```

This results in an output similar to the following:

```yaml{.nocopy}
 id
----
 17
(1 row)
```

Now consider a scenario where zip code 94085 is very popular and the target of many queries (say there was an election or a disaster in that area). As the index is distributed based on `zipcode`, everyone in zip code 94085 will end up located in the same tablet; as a result, all the queries will end up reading from that one tablet. In other words, this tablet has become hot. To avoid this, you can distribute the index on name instead of zip code, as follows:

```sql
drop index if exists idx_zip3;
create index idx_zip3 on census(name ASC, zipcode ASC) include(id);
```

Notice that we have swapped the order of columns in the index. This results in the index being distributed/ordered on name first and then ordered on zip code. Now when many queries have the same zip code, the queries will be handled by different tablets as the names being looked up will be different and will be located on different tablets.

{{<tip title="Remember">}}
Consider swapping the order of columns to avoid hot shards.
{{</tip>}}

## Distribution on more columns

Let us say you choose to distribute your index based on hash sharding so that all citizens in the same zipcode will be located in the same tablet. In that case your index might look like,

```sql{.nocopy}
create index idx_zip4 on census(zipcode HASH, name ASC) include(id);
```

Now when you look up a specific person in a certain zipcode (say, `zipcode=94085 AND name='Michael'`), it will go to just one node. But this node could become hot if there are too many lookups for that zipcode. For this, you need to add name into the sharding part of the index as,

```sql
create index idx_zip4 on census((zipcode,name) HASH) include(id);
```

Now the index data for the same zipcode would be distributed across multiple tablets as the `name` columns is also part of the sharding scheme.

{{<tip title="Remember">}}
In the case of hash sharding consider adding more columns to the sharding part to avoid hot shards.
{{</tip>}}