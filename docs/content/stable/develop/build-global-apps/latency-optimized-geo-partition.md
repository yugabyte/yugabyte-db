---
title: Latency Optimized Geo-Partitioning design pattern for global applications
headerTitle: Latency-optimized geo-partitioning
linkTitle: Latency-optimized geo-partitioning
description: Geo Partitioning for improving the latency of Multi-Active global applications
headcontent: Improve the latency of Multi-Active global applications using geo partitioning
menu:
  stable:
    identifier: global-apps-latency-optimized-geo-partition
    parent: build-global-apps
    weight: 600
type: docs
---

For multi-active applications that need to be run in multiple regions, you can opt to partition the data per region and place the replicas in nearby regions. This ensures very low latency for both reads and writes for the local partitions, providing a seamless user experience for users close to their partitions.

{{<tip>}}
Application instances are active in all regions, do consistent reads, and operate on a subset of data.
{{</tip>}}

## Overview

{{<cluster-setup-tabs>}}

Suppose you want to serve users both in the East and West regions of the US with reduced latency. Set up a cluster with a replication factor of 3 and leaders in 2 regions, `us-west-1` and `us-east-1`, and place the replicas in nearby regions, `us-west-2` and `us-east-2`. This provides low read and write latencies for the 2 partitions.

![RF3 cluster spanning 2 regions](/images/develop/global-apps/latency-optimized-geo-partition-setup.png)

## Partition your data

For the purpose of this example, create a basic table of users that you are going to partition by the `geo` field.

```plpgsql
CREATE TABLE users (
    id INTEGER NOT NULL,
    geo VARCHAR,
) PARTITION BY LIST (geo);
```

Partition your data for east and west users. This ensures that the application in `us-west` will operate on `west` partition and the application in `us-east` will operate on the `east` partition.

{{<note>}}
The tablespace definitions are discussed in the next section.
{{</note>}}

```plpgsql
--  West partition table
CREATE TABLE us_west PARTITION OF users (
   id, geo, PRIMARY KEY (id HASH, geo)
) FOR VALUES IN ('west') TABLESPACE west;

--  East partition table
CREATE TABLE us_east PARTITION OF users (
   id, geo, PRIMARY KEY (id HASH, geo)
) FOR VALUES IN ('east') TABLESPACE east;
```

![Partition your database](/images/develop/global-apps/latency-optimized-geo-partition-partition.png)

## Replica placement

Configure your `west` partition leader preference to place the leader in `us-west-1`, one replica in `us-west-2` (nearby region), and the other replica in `us-east-2`. Placing one replica of west data in the east has the advantage of enabling follower reads for applications in the east if needed.

```plpgsql
--  tablespace for west data
CREATE TABLESPACE west WITH (
    replica_placement='{"num_replicas": 3,
    "placement_blocks":[
        {"cloud":"aws","region":"us-west-1","zone":"us-west-1a","min_num_replicas":1,"leader_preference":1},
        {"cloud":"aws","region":"us-west-2","zone":"us-west-2a","min_num_replicas":1,"leader_preference":2},
        {"cloud":"aws","region":"us-east-2","zone":"us-east-2b","min_num_replicas":1}
        ]}'
);
```

![Place west replicas](/images/develop/global-apps/latency-optimized-geo-partition-west.png)

Similarly, set up your east partitions in `us-east-1`, `us-east-2`, and `us-west-2`.

```plpgsql
--  tablespace for east data
CREATE TABLESPACE east WITH (
    replica_placement='{"num_replicas": 3,
    "placement_blocks":[
        {"cloud":"aws","region":"us-east-1","zone":"us-east-1a","min_num_replicas":1,"leader_preference":1},
        {"cloud":"aws","region":"us-east-2","zone":"us-east-2a","min_num_replicas":1,"leader_preference":2},
        {"cloud":"aws","region":"us-west-2","zone":"us-west-2b","min_num_replicas":1}
        ]}'
);
```

![Place east replicas](/images/develop/global-apps/latency-optimized-geo-partition-east.png)

## Low latency

Consider the West application. As you have placed the west partition's leader in `us-west-1`, it has a low read latency of 2 ms. As this partition has a replica in a nearby region, the write latency is also low (less than 10 ms).

![West application](/images/develop/global-apps/latency-optimized-geo-partition-west-app.png)

Similarly, the East application also has low read and write latencies.

![East application](/images/develop/global-apps/latency-optimized-geo-partition-east-app.png)

## Failover

When any of the regions hosting one of the partition leaders fails, the partition follower in a nearby region would immediately be promoted to leader and the application can continue without any data loss.

![Failover](/images/develop/global-apps/latency-optimized-geo-partition-failover.png)

This pattern helps applications running in different regions to have low read and write latency, as they are reading and writing data to nearby partitions. Note that the access latency increases a little to 10 ms and the write latency increases a lot more to 60 ms. This is because the writes have to be replicated to the follower in `us-east-2`, which is 60 ms away.

## Learn more

- [Tablespaces](../../../explore/going-beyond-sql/tablespaces/)
- [Table partitioning](../../../explore/ysql-language-features/advanced-features/partitions/)
- [Row level geo-partitioning](../../../explore/multi-region-deployments/row-level-geo-partitioning/)
