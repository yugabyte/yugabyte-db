---
title: Locality Optimized Geo-Partitioning design pattern for global applications
headerTitle: Locality-optimized geo-partitioning
linkTitle: Locality-optimized geo-partitioning
description: Geo partitioning for compliance in Multi-Active global applications
headcontent: Geo partition for compliance in Multi-Active global applications
menu:
  preview:
    identifier: global-apps-locality-optimized-geo-partition
    parent: build-global-apps
    weight: 700
type: docs
---

Data residency laws (such as the [GDPR](https://en.wikipedia.org/wiki/General_Data_Protection_Regulation)) require data about a nation's citizens or residents to be collected, processed, and/or stored inside the country. Multi-national businesses must operate under local data regulations that dictate how the data of a nation's residents must be stored inside its borders. Re-architecting your storage layer and applications to support these regulations could be a very daunting task. Let us see how you can accomplish this with ease in YugabyteDB.

You would want to have data of users from different countries (for example, US, Germany, India) in the same table, but just store the rows in their regions to comply with the country's data-protection laws, or to reduce latency for the users in those countries similar to the following illustration.

{{<tip>}}
Application instances are active in all regions, do consistent reads, and data is partitioned but applications operate on the entire dataset.
{{</tip>}}

![User data stored within their country's boundaries](/images/develop/global-apps/locality-optimized-geo-partition-goal.png)

For this, YugabyteDB supports [Row-level geo-partitioning](../../../explore/multi-region-deployments/row-level-geo-partitioning/). This combines two well-known PostgreSQL concepts, [partitioning](../../../explore/ysql-language-features/advanced-features/partitions/), and [tablespaces](../../../explore/ysql-language-features/going-beyond-sql/tablespaces/).

## Overview

{{<cluster-setup-tabs>}}

Suppose you want to store the data of users from the US and Europe on their respective continents. For this, you should set up an `RF3` cluster spread across 2 geos, `US` and `Europe` to place the data of the respective geographies correctly.Also to make sure each partition acts like a global database, you should opt for 3-regions in each of the geos.

![RF3 cluster spanning 2 regions](/images/develop/global-apps/locality-optimized-geo-partition-setup.png)

## Partition your data

For this example, create a table of users, which you are going to partition by the `geo` field.

```plpgsql
CREATE TABLE users (
    id INTEGER NOT NULL,
    geo VARCHAR,
) PARTITION BY LIST (geo);
```

Now, partition your data for the US and Europe users to ensure that the application in `us-east` will operate on the `us` partition and the application in `eu-west` will operate on the `europe` partition.

{{<note>}}
The tablespace definitions are discussed in the next section.
{{</note>}}

```plpgsql
--  US partition table
CREATE TABLE us PARTITION OF users (
   id, geo, PRIMARY KEY (id HASH, geo))
) FOR VALUES IN ('us') TABLESPACE us;

--  Europe partition table
CREATE TABLE eu PARTITION OF users (
   id, geo, PRIMARY KEY (id HASH, geo))
) FOR VALUES IN ('eu') TABLESPACE eu;
```

![Partition your database](/images/develop/global-apps/locality-optimized-geo-partition-partition.png)

## Replica placement

Configure your `us` partition leader preference to place the leader in `us-east-1`, one replica in `us-east-1` (nearby region) and the other replica in `us-east-2`. (Placing one replica in the same region as the leader ensures that the local replica is up-to-date and will be quickly promoted to leader in case the leader fails). This way , all the replicas of the `US` users will be located in the `US` regions.

```plpgsql
--  tablespace for us data
CREATE TABLESPACE us WITH (
    replica_placement='{"num_replicas": 3,
    "placement_blocks":[
        {"cloud":"aws","region":"us-east-1","zone":"us-east-1a","min_num_replicas":1,"leader_preference":1},
        {"cloud":"aws","region":"us-east-2","zone":"us-east-1c","min_num_replicas":1,"leader_preference":2},
        {"cloud":"aws","region":"us-central-1","zone":"us-east-2b","min_num_replicas":1,"leader_preference":3}
        ]}'
);
```

![Place west replicas](/images/develop/global-apps/locality-optimized-geo-partition-us.png)

Similarly, set up your `europe` partitions in `eu-west-1` and `eu-west-2`.

```plpgsql
--  tablespace for Europe data
CREATE TABLESPACE eu WITH (
    replica_placement='{"num_replicas": 3,
    "placement_blocks":[
        {"cloud":"aws","region":"eu-west-1","zone":"eu-west-1a","min_num_replicas":1,"leader_preference":1},
        {"cloud":"aws","region":"eu-west-2","zone":"eu-west-1c","min_num_replicas":1,"leader_preference":2},
        {"cloud":"aws","region":"eu-central-1","zone":"eu-west-2a","min_num_replicas":1,"leader_preference":3}
    ]}'
);
```

![Place east replicas](/images/develop/global-apps/locality-optimized-geo-partition-europe.png)

This ensures all the eu-user's data will be located in Europe.

## Low latency

Consider the US application for instance. As you have placed `us` partition's leader in `us-east-1`, it has a low read latency of `2ms`. As this partition has a replica in a nearby region, the write latency is also low (`<10ms`).

![US application](/images/develop/global-apps/locality-optimized-geo-partition-us-app.png)

Similarly, the Europe application also has low read and write latencies.

![Europe application](/images/develop/global-apps/locality-optimized-geo-partition-europe-app.png)

This pattern will help applications running in different regions to have low read and write latency as they are reading and writing data to nearby partitions and at the same time, comply with local data protection laws by keeping the citizen's data inside the country boundaries.

## Global tables

Although we have partitioned the data for a single table and placed it in multiple geographies, you can also add other tables without partitioning to be present across both geographies. This way, you can have both global tables and partitioned tables in your setup.

## Failover

When any of the regions hosting one of the partition leaders fails, the partition follower in another zone would immediately be promoted to leader and the application can continue without any data loss. So this pattern is resilient for a 1-region failure.

![Failover](/images/develop/global-apps/locality-optimized-geo-partition-failover.png)

## Learn more

- [Tablespaces](../../../explore/ysql-language-features/going-beyond-sql/tablespaces/)
- [Table partitioning](../../../explore/ysql-language-features/advanced-features/partitions/)
- [Row level geo-partitioning](../../../explore/multi-region-deployments/row-level-geo-partitioning/)
