---
title: Latency Optimized Geo-Partitioning design pattern for global applications
headerTitle: Latency Optimized Geo-Partitioning
linkTitle: Latency Optimized Geo-Partitioning
description: Geo Partitioning for improving the latency of Multi-Active global applications
headcontent: Geo Partitioning for improving the latency of Multi-Active global applications
menu:
  preview:
    identifier: global-apps-latency-optimized-geo-partition
    parent: build-global-apps
    weight: 600
type: docs
---

For Multi-Active apps that need to be run in multiple regions, you can opt to partition the data per region and place the replicas in nearby regions. This would ensure very low latency for both reads and writes for the local partitions giving a seamless user experience for users close to their partitions. Let us look into this pattern in more detail.

## Overview

{{<cluster-setup-tabs>}}

Let's say you want to serve users both in the East and West regions of the US with reduced latency. For this, you should set up an `RF3` cluster with leaders in 2 regions, `us-west-1` and `us-east-1` and place the replicas in close regions, say `us-west-2` and `us-east-2`

![RF3 cluster spanning 2 regions](/images/develop/global-apps/latency-optimized-geo-partition-setup.png)

## Partition your data

For the purpose of examples, let's consider a simple table of users, which you are going to partition by the `geo` field.

```plpgsql
CREATE TABLE users (
    id INTEGER NOT NULL,
    geo VARCHAR,
) PARTITION BY LIST (geo);
```

Now partition your data for east and west users. This is to ensure that the app in `us-west` will operate on `west` partition and the app in `us-east` will operate on the `east` partition.

{{<note>}}
The tablespace definitions are discussed in the next section.
{{</note>}}

```plpgsql
--  West partition table
CREATE TABLE us_west PARTITION OF users (
   id, geo, PRIMARY KEY (id HASH, geo))
) FOR VALUES IN ('west') TABLESPACE west;

--  East partition table
CREATE TABLE us_east PARTITION OF users (
   id, geo, PRIMARY KEY (id HASH, geo))
) FOR VALUES IN ('east') TABLESPACE east;
```

![Partition your database](/images/develop/global-apps/latency-optimized-geo-partition-partition.png)

## Replica placement

Now configure your `west` partition leader preference to place the leader in `us-west-1`, one replica in `us-west-2` (nearby region) and the other replica in `us-east-2`. (Placing one replica of west data in the east has the advantage of enabling follower reads for apps in the east if needed).

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

Similarly set up your east partitions in `us-east-1`, `us-east-2` and `us-west-2`.

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

## Low Latency

Consider the West app for instance. As you have placed the west partition's leader in `us-west-1`, it has a low read latency of `2ms`. As this partition has a replica in a nearby region, the write latency is also low (`<10ms`).

![West app](/images/develop/global-apps/latency-optimized-geo-partition-west-app.png)

Similarly, the East app also has low read and write latencies.

![East app](/images/develop/global-apps/latency-optimized-geo-partition-east-app.png)

## Failover

When any of the regions hosting one of the partition leaders fails, the partition follower in a nearby region would immediately be promoted to leader and the application can continue without any data loss.

![Failover](/images/develop/global-apps/latency-optimized-geo-partition-failover.png)

This pattern will help apps running in different regions to have low read and write latency as they are reading and writing data to nearby partitions.

## Learn more

- [Tablespaces](../../../explore/ysql-language-features/going-beyond-sql/tablespaces/)
- [Table partitioning](../../../explore/ysql-language-features/advanced-features/partitions/)
- [Row level geo-partitioning](../../../explore/multi-region-deployments/row-level-geo-partitioning/)