---
title: Global ordering by time
headerTitle: Global ordering by time
linkTitle: Global ordering by time
description: Distribute your time-ordered data and retrieve data efficiently
headcontent: Distribute time-ordered data and retrieve data efficiently
menu:
  stable:
    identifier: timeseries-global-ordering
    parent: common-patterns-timeseries
    weight: 100
type: docs
---

Learn how to distribute your entire time-ordered dataset across tablets and retrieve data efficiently.

## Setup

{{<cluster-setup-tabs>}}

## Timestamp-based distribution

Consider a speed metrics tracking system that tracks the data from the speed sensor of many cars.

Create a table with an example schema as follows:

```sql
CREATE TABLE global_order1 (
    ts timestamp, /* time at which the event was generated */
    car varchar, /* name of the car */
    speed int, /* speed of your car */
    PRIMARY KEY(ts ASC)
);
```

The `global_order1` table stores the speed data points of different cars as they arrive in the system.

Insert some sample data into the table as follows:

```sql
INSERT INTO global_order1 (ts, car, speed)
        (SELECT '2023-07-01 00:00:00'::timestamp + make_interval(secs=>id),
            'car-' || ceil(random()*2), ceil(random()*60)
            FROM generate_series(1,100) AS id);
```

Retrieve some data from the table as follows:

```sql
SELECT * FROM global_order1;
```

```output
         ts          |  car  | speed
---------------------+-------+-------
 2023-07-01 00:00:01 | car-1 |    50
 2023-07-01 00:00:02 | car-2 |    25
 2023-07-01 00:00:03 | car-1 |    39
 2023-07-01 00:00:04 | car-1 |    49
 2023-07-01 00:00:05 | car-2 |     3
 2023-07-01 00:00:06 | car-2 |    22
 2023-07-01 00:00:07 | car-2 |    25
 2023-07-01 00:00:08 | car-1 |    58
 2023-07-01 00:00:09 | car-2 |    55
```

Notice how the data is automatically ordered by time. This is because the table is set to be sorted on the `ts` by `PRIMARY KEY(ts ASC)`. This ensures that the data is sorted and all nearby data resides in the same tablet. This order makes it efficient for range queries to retrieve all data within a specific time range. For example:

```sql
SELECT * FROM global_order1 WHERE ts > '2023-07-01 00:01:00' AND ts < '2023-07-01 00:01:05';
```

```output
         ts          |  car  | speed
---------------------+-------+-------
 2023-07-01 00:01:01 | car-1 |    21
 2023-07-01 00:01:02 | car-2 |    58
 2023-07-01 00:01:03 | car-1 |    57
 2023-07-01 00:01:04 | car-2 |    60
```

As the amount of data grows, the tablet splits and half moves to a different tablet, ensuring scalability. This also means that the data grows in one shard before moving to the next tablet. However, because a specific range could be in a single shard, this could lead to one shard becoming a hot shard.

## Bucket-based distribution

To distribute ordered data across different tablets, you can use bucket-based distribution, where data is split into buckets and then distributed.

To do this, modify the table to include a `bucketid` field that would have a small range of values, and distribute the buckets. For example:

```sql
CREATE TABLE global_order2 (
    ts timestamp,/* time at which the event was generated */
    car varchar, /* name of the car */
    speed int,   /* speed of your car */
    bucketid smallint DEFAULT random()*8, /* bucket id*/
    PRIMARY KEY(bucketid HASH, ts ASC)
) SPLIT INTO 3 TABLETS;
```

{{<note>}}The table is explicitly split into three tablets to better view the tablet information in the following examples.{{</note>}}

This adds a `bucketid` to your data, consisting of a random number between `0` and `7`, and which you will use to distribute the data on the entity and `bucketid`.

Add the same data to the new table as follows:

```sql
INSERT INTO global_order2 (ts, car, speed)
        (SELECT '2023-07-01 00:00:00'::timestamp + make_interval(secs=>id),
            'car-' || ceil(random()*2), ceil(random()*60)
            FROM generate_series(1,100) AS id);
```

Because the default value of `bucketid` is set to `random()*8`, you do not have to explicitly insert the value.

Retrieve the data from the table as follows:

```sql
SELECT *, yb_hash_code(bucketid) % 3 as tablet FROM global_order2;
```

```output
         ts          |  car  | speed | bucketid | tablet
---------------------+-------+-------+----------+--------
 2023-07-01 00:00:24 | car-2 |    19 |        4 |      2
 2023-07-01 00:00:25 | car-1 |    21 |        4 |      2
 2023-07-01 00:00:26 | car-1 |    40 |        4 |      2
...
 2023-07-01 00:00:35 | car-1 |    46 |        0 |      1
 2023-07-01 00:00:40 | car-1 |    16 |        0 |      1
 2023-07-01 00:00:41 | car-2 |    34 |        0 |      1
...
 2023-07-01 00:00:22 | car-2 |    57 |        0 |      0
 2023-07-01 00:00:35 | car-1 |    46 |        0 |      0
 2023-07-01 00:00:40 | car-1 |    16 |        0 |      0
```

Notice that the data is split into buckets and the buckets are distributed across different tablets. The data is ordered by `ts` in each bucket, but your result is not ordered.

Because the query planner does not know about the different values of `bucketid`, it must perform a sequential scan for the preceding query. To efficiently retrieve all the data for a specific car, say `car-1`, modify the query to explicitly call out the buckets as follows:

```sql
SELECT * FROM global_order2 WHERE bucketid IN (0,1,2,3,4,5,6,7) ORDER BY ts ASC;
```

```bash
         ts          |  car  | speed | bucketid
---------------------+-------+-------+----------
 2023-07-01 00:00:01 | car-2 |     7 |        1
 2023-07-01 00:00:02 | car-1 |    27 |        6
 2023-07-01 00:00:03 | car-1 |    32 |        1
 2023-07-01 00:00:04 | car-2 |    28 |        6
 2023-07-01 00:00:05 | car-2 |    45 |        2
 2023-07-01 00:00:06 | car-1 |    14 |        3
 2023-07-01 00:00:07 | car-1 |    14 |        1
 2023-07-01 00:00:08 | car-1 |    35 |        5
 2023-07-01 00:00:09 | car-1 |    58 |        3
```

You can execute an example query plan to verify that the preceding query uses the primary key index as follows:

```sql
EXPLAIN ANALYZE SELECT * FROM global_order2 WHERE bucketid IN (0,1,2,3,4,5,6,7) ORDER BY ts ASC;
```

```sql{.nocopy}
                                               QUERY PLAN
--------------------------------------------------------------------------------------------------------
 Sort (actual time=2.279..2.289 rows=95 loops=1)
   Output: ts, car, speed, bucketid
   Sort Key: global_order2.ts
   Sort Method: quicksort  Memory: 32kB
   ->  Index Scan using global_order2_pkey on public.global_order2 (actual time=2.139..2.207 rows=95 loops=1)
         Output: ts, car, speed, bucketid
         Index Cond: (global_order2.bucketid = ANY ('{0,1,2,3,4,5,6,7}'::integer[]))
 Planning Time: 0.180 ms
 Execution Time: 2.388 ms
 Peak Memory Usage: 34 kB
```

This is how you can efficiently retrieve data from the distributed buckets in the correct order.

## Learn more

- [Avoiding hotspots on Range-based data](https://www.yugabyte.com/blog/distributed-databases-hotspots-range-based-indexes/)
- [Pagination for Distributed and Ordered data](https://www.yugabyte.com/blog/optimize-pagination-distributed-data-maintain-ordering/)
