---
title: Sharding
linkTitle: Sharding
description: Sharding into Tablets
aliases:
  - /latest/architecture/docdb/sharding/
  - /latest/architecture/concepts/sharding/
  - /latest/architecture/concepts/docdb/sharding/
menu:
  latest:
    identifier: docdb-sharding
    parent: docdb
    weight: 1142
isTocNested: false
showAsideToc: false
---

User tables are implicitly managed as multiple shards by DocDB. These shards are referred to as
**tablets**. The primary key for each row in the table uniquely determines the tablet the row lives in. This is shown in the figure below.

![Partitioning a table into tablets](/images/architecture/partitioning-table-into-tablets.png)


For data distribution purposes, a hash based partitioning scheme is used.

{{< note title="Note" >}}
In order to enable use cases such as ordered secondary indexes, support for range-partitioned tables is in the works.
{{< /note >}}

## Hash Partitioning Tables

The hash space for hash partitioned YugaByte DB tables is the 2-byte range from 0x0000 to 0xFFFF. Such
a table may therefore have at most 64K tablets. We expect this to be sufficient in practice even for
very large data sets or cluster sizes.

As an example, for a table with 16 tablets the overall hash space [0x0000 to 0xFFFF) is divided into
16 sub-ranges, one for each tablet:  [0x0000, 0x1000), [0x1000, 0x2000), … , [0xF000, 0xFFFF).

![tablet_overview](/images/architecture/tablet_overview.png)

Read/write operations are processed by converting the primary key into an internal key and its hash
value, and determining what tablet the operation should be routed to.

The figure below illustrates this.

![tablet_hash](/images/architecture/tablet_hash.png)

{{< note title="Note" >}}
For every given key, there is exactly one tablet that owns it.
{{< /note >}}

The insert/update/upsert by the end user is processed by serializing and hashing the primary key into byte-sequences and determining the tablet they belong to. Let us assume that the user is trying to insert a key k with a value v into a table T. The figure below illustrates how the tablet owning the key for the above table is determined.

![tablet_hash_2](/images/architecture/tablet_hash_2.png)

