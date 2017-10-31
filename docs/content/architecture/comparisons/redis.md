---
date: 2016-03-09T20:08:11+01:00
title: YugaByte DB vs. Redis
weight: 42
---

## Persistent DB vs. In-Memory Cache

YugaByte DB’s Redis is a persistent database rather than an in-memory cache. [While Redis has a
check-pointing feature for persistence, it is a highly inefficient operation that does a process
fork. It is also not an incremental operation; the entire memory state is written to disk causing
serious overall performance impact.]

## Auto Sharded and Clustered

YugaByte DB’s Redis is an auto sharded, clustered with built-in support for strongly consistent
replication and multi-DC deployment flexibility. Operations such as add node, remove node are
simple, throttled and intent-based and leverage YugaByte’s core engine (YBase) and associated
architectural benefits.

## No Explicit Memory Management

Unlike the normal Redis, the entire data set does not need to fit in memory. In YugaByte, the hot
data lives in RAM, and colder data is automatically tiered to storage and on-demand paged in at
block granularity from storage much like traditional database.


## Consistent and Transparent Caching

Applications that use Redis only as a cache and use a separate backing database as the main system
of record, and need to deal with dev pain points around keeping the cache and DB consistent and
operational pain points at two levels of infrastructure (sharding, load-balancing, geo-redundancy)
etc. can leverage YugaByte DB’s Redis as a unified cache + database offering.

Scan resistant block cache design ensures long scan (e.g., of older data) do not impact reads for
recent data.
