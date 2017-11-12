---
date: 2016-03-09T20:08:11+01:00
title: YugaByte DB vs. Apache HBase
weight: 53
---

## Simpler software stack

HBase relies on HDFS (another complex piece of infrastructure for data replication) and on Zookeeper for leader election, failure detection, and so on. Running HBase smoothly requires a lot of on-going operational overheads.

## High availability / Fast-failover

In HBase, on a region-server death, the unavailability window of shards/regions
on the server can be in the order of 60 seconds or more. This is because the HBase master first
needs to wait for the server’s ephemeral node in Zookeeper to expire, followed by time take to split
the transaction logs into per-shard recovery logs, and the time taken to replay the edits from the
transaction log by a new server before the shard is available to take IO. In contrast, in YugaByte,
the tablet-peers are hot standbys, and within a matter of few heartbeats (about few seconds) detect
failure of the leader, and initiate leader election.

## C++ implementation

Avoids GC tuning; can run better on large memory machines.
Richer data model: YugaByte offers a multi-model/multi-API through CQL & Redis (and SQL in future).
Rather than deal with just byte keys and values, YugaByte offers a rich set of scalar (int, text,
decimal, binary, timestamp, etc.) and composite types (such as collections, UDTs, etc.).

## Multi-DC deployment

Flexlible deployment choices across multiple DCs or availability zones. HBase provides
strong-consistency only within a single datacenter and offers only async replication alternative for
cross-DC deployments.
