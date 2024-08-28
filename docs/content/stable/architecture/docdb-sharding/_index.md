---
title: DocDB sharding layer
headerTitle: DocDB sharding layer
linkTitle: Sharding
description: Learn about sharding strategies, hash and range sharding, colocated tables, and table splitting.
image: fa-sharp fa-thin fa-grip-vertical
headcontent: Learn about sharding strategies, hash and range sharding, colocated tables, and table splitting.
menu:
  stable:
    identifier: architecture-docdb-sharding
    parent: architecture
    weight: 700
type: indexpage
---

A distributed SQL database needs to automatically split the data in a table and distribute it across nodes. This is known as data sharding and it can be achieved through different strategies, each with its own tradeoffs. YugabyteDB's sharding architecture is inspired by <a href="https://research.google.com/archive/spanner-osdi2012.pdf">Google Spanner</a>.

## Sharding

YugabyteDB splits table data into smaller pieces called [tablets a.k.a shards](../key-concepts#tablet). Sharding is the process of mapping of a row of a table to a shard. Sharding helps in scalability and geo-distribution by horizontally partitioning data. These shards are distributed across multiple server nodes (containers, virtual machines, bare-metal) in a shared-nothing architecture. The application interacts with a SQL table as one logical unit and remains agnostic to the physical placement of the shards. DocDB supports range and hash sharding natively.

{{<lead link="sharding/">}}
To know more about the different sharding strategies and how they work, see [Sharding strategies](sharding/).
{{</lead>}}

## Tablet splitting

As table data grows, the size of tablets increase. Once a tablet reaches a threshold size, it automatically splits into two. These 2 new tablets can now be placed in other nodes to keep the load on the system balanced. Tablet splitting is one of the foundations of [scaling](../../explore/linear-scalability).

{{<lead link="tablet-splitting/">}}
To understand how and when tablets split, see [Tablet splitting](tablet-splitting/).
{{</lead>}}
