---
title: Order by entity
headerTitle: Order by entity
linkTitle: Order by entity
description: Keep entity data together using Entity-wise or Bucket-based ordering
headcontent: Keep entity data together in a time series data model
menu:
  preview:
    identifier: timeseries-entity-ordering
    parent: common-patterns-timeseries
    weight: 200
type: docs
---

In a time series data model, to enforce that all data for an entity stays together while maintaining the timestamp-based ordering, you have to distribute the data by the entity and order it by time.

The following sections describe how to order by entity with a few examples.

## Setup

{{<cluster-setup-tabs>}}

## Entity-wise ordering

Consider a speed metrics tracking system that tracks the data from the speed sensor of many cars.

Create a table with an example schema as follows:

```sql
CREATE TABLE entity_order1 (
    ts timestamp,/* time at which the event was generated */
    car varchar, /* name of the car */
    speed int,   /* speed of your car */
    PRIMARY KEY(car HASH, ts ASC)
) SPLIT INTO 3 TABLETS;
```

{{<note>}}The table is explicitly split into three tablets to better view the tablet information in the following examples.{{</note>}}

When you insert data, it is distributed on the value of `yb_hash_code(car)`, but within a car the data is ordered by timestamp.

Insert data into the table as follows:

```sql
INSERT INTO entity_order1 (ts, car, speed)
        (SELECT '2023-07-01 00:00:00'::timestamp + make_interval(secs=>id),
            'car-' || ceil(random()*2), ceil(random()*60)
            FROM generate_series(1,100) AS id);
```

Retrieve some data from the table as follows:

```sql
SELECT *, yb_hash_code(car) % 3 as tablet FROM entity_order1;
```

```output
        ts          |  car  | speed | tablet
---------------------+-------+-------+--------
 2023-07-01 00:00:03 | car-2 |    50 |      1
 2023-07-01 00:00:05 | car-2 |     8 |      1
 2023-07-01 00:00:07 | car-2 |    19 |      1
 2023-07-01 00:00:09 | car-2 |    56 |      1
 ...
 2023-07-01 00:00:01 | car-1 |     2 |      2
 2023-07-01 00:00:02 | car-1 |    38 |      2
 2023-07-01 00:00:04 | car-1 |    51 |      2
 2023-07-01 00:00:06 | car-1 |    10 |      2
```

Notice that the data for each car is together (in the same tablet), but at the same time, the data is automatically sorted. The key thing to note here is that all the data for a specific car (say `car-1`) will be located in the same tablet (`2`), because you have defined the data to be distributed on the hash of `car` (`PRIMARY KEY(car HASH, ts ASC)`).

Distributing the data by the entity (`car`) and ordering the data by timestamp for each entity solves the problem of keeping data together for an entity and at the same time maintains a global distribution across different entities across the different tablets. But this could lead to a hot shard problem if there are too many operations on the same car.

## Bucket-based distribution

One way to overcome the problem of hot shards is to use bucket-based distribution.

Bucketing allows you to distribute your data on a specific entity and at the same time keep the data ordered in the entity. The idea is to split the entities' data into buckets and distribute the buckets. To understand this, modify the preceding table to add a `bucketid`, as follows:

```sql
CREATE TABLE entity_order2 (
    ts timestamp,/* time at which the event was generated */
    car varchar, /* name of the car */
    speed int,   /* speed of your car */
    bucketid smallint DEFAULT random()*8, /* bucket id*/
    PRIMARY KEY((car, bucketid) HASH, ts ASC)
) SPLIT INTO 3 TABLETS;
```

This adds a `bucketid` to your data, consisting of a random number between `0` and `7`, and which you will use to distribute the data on the entity and `bucketid`.

Add the same data to the new table as follows:

```sql
INSERT INTO entity_order2 (ts, car, speed)
        (SELECT '2023-07-01 00:00:00'::timestamp + make_interval(secs=>id),
            'car-' || ceil(random()*2), ceil(random()*60)
            FROM generate_series(1,100) AS id);
```

Because the default value of `bucketid` is set to `random()*8`, you do not have to explicitly insert the value.

Retrieve the data from the table as follows:

```sql
SELECT *, yb_hash_code(car,bucketid) % 3 as tablet FROM entity_order2;
```

```output
         ts          |  car  | speed | bucketid | tablet
---------------------+-------+-------+----------+--------
 2023-07-01 00:00:21 | car-1 |    45 |        7 |      2
 2023-07-01 00:00:22 | car-1 |     9 |        7 |      2
 2023-07-01 00:00:37 | car-1 |    32 |        7 |      2
 ...
 2023-07-01 00:00:27 | car-2 |    26 |        1 |      2
 2023-07-01 00:00:40 | car-2 |    26 |        1 |      2
 2023-07-01 00:01:10 | car-2 |    46 |        1 |      2
 ...
 2023-07-01 00:00:08 | car-1 |    43 |        2 |      1
 2023-07-01 00:00:13 | car-1 |    20 |        2 |      1
 2023-07-01 00:00:20 | car-1 |    53 |        2 |      1
```

Notice that the data for each car is split into buckets and that the data is ordered on by `ts` in each bucket, and that the buckets are distributed across different tablets.

Because the query planner does not know about the different values of `bucketid`, it must perform a sequential scan for the preceding query. To efficiently retrieve all the data for a specific car, say `car-1`, modify the query to explicitly call out the buckets as follows:

```sql
SELECT * FROM entity_order2
    WHERE car='car-1' AND bucketid IN (0,1,2,3,4,5,6,7);
```

```output
         ts          |  car  | speed | bucketid
---------------------+-------+-------+----------
 2023-07-01 00:00:21 | car-1 |    45 |        7
 2023-07-01 00:00:22 | car-1 |     9 |        7
 2023-07-01 00:00:37 | car-1 |    32 |        7
 2023-07-01 00:00:41 | car-1 |    51 |        7
 2023-07-01 00:00:57 | car-1 |    50 |        7
 2023-07-01 00:01:09 | car-1 |    59 |        7
 2023-07-01 00:01:23 | car-1 |    54 |        7
```

This enables the query planner to use the primary index on `car, bucketid`, as now it knows the values for `car` and the `bucketid` to look for.

```sql
EXPLAIN ANALYZE SELECT * FROM entity_order2 WHERE car='car-1' AND bucketid IN (0,1,2,3,4,5,6,7);
```

```sql{.nocopy}
                                                              QUERY PLAN
------------------------------------------------------------------------------------------------------------------------------
 Index Scan using entity_order2_pkey on entity_order2  (cost=0.00..16.25 rows=100 width=46) (actual time=1.534..1.562 rows=49 loops=1)
   Index Cond: (((car)::text = 'car-1'::text) AND (bucketid = ANY ('{0,1,2,3,4,5,6,7}'::integer[])))
 Planning Time: 0.129 ms
 Execution Time: 1.624 ms
 Peak Memory Usage: 8 kB
```

You can see that the data is not truly sorted in the result set. This is because the data is ordered only in each bucket. Add the `order by` clause to your original query as follows:

```sql
SELECT * FROM entity_order2 WHERE car='car-1' AND bucketid IN (0,1,2,3,4,5,6,7) ORDER BY ts ASC;
```

```output
         ts          |  car  | speed | bucketid
---------------------+-------+-------+----------
 2023-07-01 00:00:01 | car-1 |    57 |        4
 2023-07-01 00:00:03 | car-1 |     7 |        5
 2023-07-01 00:00:04 | car-1 |    58 |        6
 2023-07-01 00:00:07 | car-1 |    48 |        3
 2023-07-01 00:00:08 | car-1 |    43 |        2
 2023-07-01 00:00:12 | car-1 |    60 |        1
 2023-07-01 00:00:13 | car-1 |    20 |        2
```

Now you can see that the data is correctly ordered on `ts`.

## Learn more

- [Avoiding hotspots on Range-based data](https://www.yugabyte.com/blog/distributed-databases-hotspots-range-based-indexes/)
- [Pagination for Distributed and Ordered data](https://www.yugabyte.com/blog/optimize-pagination-distributed-data-maintain-ordering/)
