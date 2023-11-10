---
title: Partition data by time
headerTitle: Partition data by time
linkTitle: Partition data by time
description: Partition data for efficient data management
headcontent: Partition data for efficient data management
menu:
  stable:
    identifier: timeseries-partition-by-time
    parent: common-patterns-timeseries
    weight: 400
type: docs
---

Partitioning refers to splitting what is logically one large table into smaller physical pieces. The key advantage of partitioning in YugabyteDB is that, because each partition is a separate table, it is efficient to keep the most significant (for example, most recent) data in one partition, and not-so-important data in other partitions so that they can be dropped easily.

The following example describes the advantages of partitions in more detail.

{{<note title="Note">}}
Partitioning is only available in [YSQL](../../../../api/ysql/).
{{</note>}}

## Setup

{{<cluster-setup-tabs>}}

Consider a scenario where you have a lot of data points from cars and you care about only the last month's data. Although you can execute a statement to delete data that is older than 30 days, because data is not immediately removed from the underlying storage (LSM-based DocDB), it could affect the performance of scans.

Create a table with an example schema as follows:

```sql
CREATE TABLE part_demo (
    ts timestamp,/* time at which the event was generated */
    car varchar, /* name of the car */
    speed int,   /* speed of your car */
    PRIMARY KEY(car HASH, ts ASC)
) PARTITION BY RANGE (ts);
```

Create partitions for each month. Also, create a `DEFAULT` partition for data that does not fall into any of the other partitions.

```sql
CREATE TABLE part_7_23 PARTITION OF part_demo
    FOR VALUES FROM ('2023-07-01') TO ('2023-08-01');

CREATE TABLE part_8_23 PARTITION OF part_demo
    FOR VALUES FROM ('2023-08-01') TO ('2023-09-01');

CREATE TABLE part_9_23 PARTITION OF part_demo
    FOR VALUES FROM ('2023-09-01') TO ('2023-10-01');

CREATE TABLE def_part_demo PARTITION OF part_demo DEFAULT;
```

Insert some data into the main table `part_demo`:

```sql
INSERT INTO part_demo (ts, car, speed)
    (SELECT '2023-07-01 00:00:00'::timestamp +
        make_interval(secs=>id, months=>((random()*2)::int)),
        'car-' || ceil(random()*2), ceil(random()*60)
        FROM generate_series(1,100) AS id);
```

If you retrieve the rows from the respective partitions, notice that they have the rows for their respective date ranges. For example:

```sql
SELECT * FROM part_9_23 LIMIT 4;
```

```output
         ts          |  car  | speed
---------------------+-------+-------
 2023-09-01 00:00:04 | car-2 |    45
 2023-09-01 00:00:05 | car-2 |    38
 2023-09-01 00:00:08 | car-2 |    49
 2023-09-01 00:00:23 | car-2 |    33
```

## Fetch data

Although data is stored in different tables as partitions, to access all the data, you just need to query the parent table. Take a look at the query plan for a select query as follows:

```sql
EXPLAIN ANALYZE SELECT * FROM part_demo;
```

```output
                                  QUERY PLAN
-------------------------------------------------------------------------------
 Append (actual time=1.085..5.351 rows=100 loops=1)
   ->  Seq Scan on public.part_7_23 (actual time=1.079..2.431 rows=25 loops=1)
         Output: part_7_23.ts, part_7_23.car, part_7_23.speed
   ->  Seq Scan on public.part_8_23 (actual time=0.665..1.555 rows=47 loops=1)
         Output: part_8_23.ts, part_8_23.car, part_8_23.speed
   ->  Seq Scan on public.part_9_23 (actual time=0.648..1.342 rows=28 loops=1)
         Output: part_9_23.ts, part_9_23.car, part_9_23.speed
 Planning Time: 0.105 ms
 Execution Time: 5.434 ms
 Peak Memory Usage: 19 kB
```

When querying the parent table, the child partitions are automatically queried.

## Fetch data in a time range

As the data is split based on time, when querying for a specific time range, the query executor fetches data only from the partition that the data is expected to be in. For example, see the query plan for fetching data for a specific month:

```sql
EXPLAIN ANALYZE SELECT * FROM part_demo WHERE ts > '2023-07-01' AND ts < '2023-08-01';
```

```sql{.nocopy}
                                   QUERY PLAN
-----------------------------------------------------------------------------------------------------------
 Append (actual time=2.288..2.310 rows=25 loops=1)
   ->  Seq Scan on public.part_7_23 (actual time=2.285..2.301 rows=25 loops=1)
         Output: part_7_23.ts, part_7_23.car, part_7_23.speed
         Remote Filter: ((part_7_23.ts > '2023-07-01 00:00:00'::timestamp without time zone)
                AND (part_7_23.ts < '2023-08-01 00:00:00'::timestamp without time zone))
 Planning Time: 0.309 ms
 Execution Time: 2.411 ms
 Peak Memory Usage: 14 kB
```

You can see that the planner has chosen only one partition to fetch the data from.

## Dropping older data

The key advantage of data partitioning is to drop older data easily. To drop the older data, all you need to do is drop that particular partition table. For example, when month `7`'s data is not needed, do the following:

```sql
DROP TABLE part_7_23;
```

```output
DROP TABLE
Time: 103.214 ms
```

## Learn more

- [Table partitioning](../../../../explore/ysql-language-features/advanced-features/partitions/)
