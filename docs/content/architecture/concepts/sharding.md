---
title: Data Sharding into Tablets
weight: 940
---

User tables are implicitly managed as multiple shards by the system. These shards are referred to as
**tablets**. The primary key for each row in the table uniquely determines the tablet the row lives in.
For data distribution purposes, a hash based partitioning scheme is used. [Note: For some use cases,
such as ordered secondary indexes, we’ll also support range-partitioned tables. We’ll discuss that
topic in the future.]

The hash space for hash partitioned YugaByte tables is the 2-byte range from 0x0000 to 0xFFFF. Such
a table may therefore have at most 64K tablets. We expect this to be sufficient in practice even for
very large data sets or cluster sizes.

As an example, for a table with 16 tablets the overall hash space [0x0000 to 0xFFFF) is divided into
16 sub-ranges, one for each tablet:  [0x0000, 0x1000), [0x1000, 0x2000), … , [0xF000, 0xFFFF).

Read/write operations are processed by converting the primary key into an internal key and its hash
value, and determining what tablet the operation should be routed to.

The figure below illustrates this.

![tablet_overview](/images/tablet_overview.png)

For every given key, there is exactly one tablet that owns it. The insert/update/upsert by the end
user is processed by serializing and hashing the primary key into byte-sequences and determining the
tablet they belong to. Let us assume that the user is trying to insert a key k with a value v into a
table T. The figure below illustrates how the tablet owning the key for the above table is
determined.

![tablet_hash](/images/tablet_hash.png)
![tablet_hash_2](/images/tablet_hash_2.png)

