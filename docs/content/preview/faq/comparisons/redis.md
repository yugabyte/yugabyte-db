---
title: Compare Redis in-memory store with YugabyteDB
linkTitle: Redis in-memory store
description: Compare Redis in-memory store with the YugabyteDB database.
aliases:
  - /comparisons/redis/
menu:
  preview_faq:
    parent: comparisons
    identifier: comparisons-redis
    weight: 1129
type: docs
---

Following are the key areas of differences between YugabyteDB and [Redis in-memory store](https://redis.io/).

## Persistent DB vs. in-memory cache

YugabyteDB is a persistent database rather than an in-memory cache. (While Redis has a check-pointing feature for persistence, it is a highly inefficient operation that does a process fork. It is also not an incremental operation; the entire memory state is written to disk causing serious overall performance impact.)

## Auto-sharded and clustered

YugabyteDB is an auto-sharded, clustered with built-in support for strongly consistent replication and multi-DC deployment flexibility. Operations such as add node, remove node are simple, throttled, and intent-based, and leverage YugabyteDB's architectural benefits.

## No explicit memory management

Unlike Redis, the entire data set does not need to fit in memory. In Yugabyte, the hot data lives in RAM. Colder data is automatically tiered to storage, and on-demand paged in at block granularity from storage much like traditional database.

## Consistent and transparent caching

Applications that use Redis only as a cache and use a separate backing database as the main system of record, and need to deal with pain points around keeping the cache and DB consistent and operational pain points at two levels of infrastructure (sharding, load-balancing, geo-redundancy) can leverage YugabyteDB as a unified cache + database offering.

Scan resistant block cache design ensures long scans (such as of older data) do not impact reads for recent data.
