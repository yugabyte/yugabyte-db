---
title: Locality Optimized Geo-Partitioning design pattern for global applications
headerTitle: Locality Optimized Geo-Partitioning
linkTitle: Locality Optimized Geo-Partitioning
description: Geo Partitioning for compliance in Multi-Active global applications
headcontent: Geo Partitioning for compliance in Multi-Active global applications
menu:
  preview:
    identifier: global-apps-locality-optimized-geo-partition
    parent: build-global-apps
    weight: 700
type: docs
---

Data residency laws require data about a nation's citizens or residents to be collected, processed, and/or stored inside the country. Multi-national businesses must operate under local data regulations that dictate how the data of a nation's residents must be stored within its borders. Re-architecting your storage layer and applications to support these could be a very daunting task. Let us see how you can accomplish this with ease in YugabyteDB.

You would want to have data of users from different countries (eg. US/Germany/India) in the same table, but just store the rows in their regions to comply with the country's data-protection laws (eg. [GDPR](https://en.wikipedia.org/wiki/General_Data_Protection_Regulation)), or to reduce latency for the users in those countries. Something similar to the illustration below.

![User data stored within their country's boundaries](/images/develop/global-apps/locality-optimized-geo-partition-goal.png)

For this, YugabyteDB supports [Row-level geo-partitioning](../../../explore/multi-region-deployments/row-level-geo-partitioning/). This combines two well-known PostgreSQL concepts, [partitioning](../../../explore/ysql-language-features/advanced-features/partitions/), and [tablespaces](../../../explore/ysql-language-features/going-beyond-sql/tablespaces/).

## Overview

{{<cluster-setup-tabs>}}

Let's say you want to store the data of users from the US and Europe within their respective continents. For this, you should set up an `RF3` cluster with leaders in 2 regions, `eu-west-1` and `eu-west-1` and place the replicas in close regions, say `eu-west-2` and `eu-west-2`

![RF3 cluster spanning 2 regions](/images/develop/global-apps/locality-optimized-geo-partition-setup.png)

## Partition your data

For the purpose of examples, let's consider a simple table of users, which you are going to partition by the `geo` field.

```plpgsql
CREATE TABLE users (
    id INTEGER NOT NULL,
    geo VARCHAR,
) PARTITION BY LIST (geo);
```

Now partition your data for US and Europe users. This is to ensure that the app in `us-east` will operate on the `us` partition and the app in `eu-west` will operate on the `europe` partition.

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

Now configure your `us` partition leader preference to place the leader in `us-east-1`, one replica in `us-east-1` (nearby region) and the other replica in `us-east-2`. (Placing one replica in the same region as the leader ensures that the local replica is up-to-date and will be quickly promoted to leader in case the leader fails). This way , all the replicas of the `US` users will be located in the `US` regions.

```plpgsql
--  tablespace for us data
CREATE TABLESPACE us WITH (
    replica_placement='{"num_replicas": 3, 
    "placement_blocks":[
        {"cloud":"aws","region":"us-east-1","zone":"us-east-1a","min_num_replicas":1,"leader_preference":1},
        {"cloud":"aws","region":"us-east-1","zone":"us-east-1c","min_num_replicas":1,"leader_preference":2},
        {"cloud":"aws","region":"us-east-2","zone":"us-east-2b","min_num_replicas":1,"leader_preference":3}
        ]}'
);
```

![Place west replicas](/images/develop/global-apps/locality-optimized-geo-partition-us.png)

Similarly set up your `europe` partitions in `eu-west-1` and `eu-west-2`.

```plpgsql
--  tablespace for Europe data
CREATE TABLESPACE eu WITH (
    replica_placement='{"num_replicas": 3, 
    "placement_blocks":[
        {"cloud":"aws","region":"eu-west-1","zone":"eu-west-1a","min_num_replicas":1,"leader_preference":1},
        {"cloud":"aws","region":"eu-west-1","zone":"eu-west-1c","min_num_replicas":1,"leader_preference":2},
        {"cloud":"aws","region":"eu-west-2","zone":"eu-west-2a","min_num_replicas":1,"leader_preference":3}
    ]}'
);
```

![Place east replicas](/images/develop/global-apps/locality-optimized-geo-partition-europe.png)

This ensures all the eu-user's data will be located within Europe.

## Low Latency

Consider the US app for instance. As you have placed the `us` partition's leader in `us-east-1`, it has a low read latency of `2ms`. As this partition has a replica in a nearby region, the write latency is also low (`<10ms`).

![US app](/images/develop/global-apps/locality-optimized-geo-partition-us-app.png)

Similarly, the Europe app also has low read and write latencies.

![Europe app](/images/develop/global-apps/locality-optimized-geo-partition-europe-app.png)

This pattern will help apps running in different regions to have low read and write latency as they are reading and writing data to nearby partitions and at the same time, comply to local data protection laws by keeping the citizen's data within the country boundaries.

## Learn more

- [Tablespaces](../../../explore/ysql-language-features/going-beyond-sql/tablespaces/)
- [Table partitioning](../../../explore/ysql-language-features/advanced-features/partitions/)
- [Row level geo-partitioning](../../../explore/multi-region-deployments/row-level-geo-partitioning/)