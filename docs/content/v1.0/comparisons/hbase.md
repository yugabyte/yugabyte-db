---
title: Apache HBase
linkTitle: Apache HBase
description: Apache HBase
menu:
  v1.0:
    parent: comparisons
    weight: 1130
---

Following are the key areas of differences between YugaByte DB and [Apache HBase](http://hbase.apache.org/).

## 1. Simpler Software Stack

HBase relies on HDFS (another complex piece of infrastructure for data replication) and on Zookeeper for leader election, failure detection, and so on. Running HBase smoothly requires a lot of on-going operational overheads.

## 2. High Availability / Fast-Failover

In HBase, on a region-server death, the unavailability window of shards/regions
on the server can be in the order of 60 seconds or more. This is because the HBase master first
needs to wait for the server’s ephemeral node in Zookeeper to expire, followed by time take to split
the transaction logs into per-shard recovery logs, and the time taken to replay the edits from the
transaction log by a new server before the shard is available to take IO. In contrast, in YugaByte,
the tablet-peers are hot standbys, and within a matter of few heartbeats (about few seconds) detect
failure of the leader, and initiate leader election.

## 3. C++ Implementation

Avoids GC tuning; can run better on large memory machines.
Richer data model: YugaByte offers a multi-model/multi-API through CQL & Redis (and SQL in future).
Rather than deal with just byte keys and values, YugaByte offers a rich set of scalar (int, text,
decimal, binary, timestamp, etc.) and composite types (such as collections, UDTs, etc.).

## 4. Multi-Datacenter deployment

Flexlible deployment choices across multiple DCs or availability zones. HBase provides
strong-consistency only within a single datacenter and offers only async replication alternative for
cross-DC deployments.
