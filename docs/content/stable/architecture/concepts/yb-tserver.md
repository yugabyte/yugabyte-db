---
title: YB-TServer service
headerTitle: YB-TServer service
linkTitle: YB-TServer service
description: Learn how the YB-TServer service stores and serves application data using tablets (also known as shards).
menu:
  stable:
    identifier: architecture-concepts-yb-tserver
    parent: key-concepts
    weight: 1124
type: docs
---

The YugabyteDB Tablet Server (YB-TServer) service is responsible for the input-output (I/O) of the end-user requests in a YugabyteDB cluster. Data for a table is split (sharded) into tablets. Each tablet is composed of one or more tablet peers, depending on the replication factor. Each YB-TServer hosts one or more tablet peers.

The following diagram depicts a basic four-node YugabyteDB universe, with one table that has 16 tablets and a replication factor of 3:

![tserver_overview](/images/architecture/tserver_overview-1.png)

The tablet-peers corresponding to each tablet hosted on different YB-TServers form a Raft group and replicate data between each other. The system shown in the preceding diagram includes sixteen independent Raft groups. For more information, see [Replication layer](../../docdb-replication/).

Within each YB-TServer, cross-tablet intelligence is employed to maximize resource efficiency. There are multiple ways the YB-TServer coordinates operations across tablets it hosts.

## Server-global block cache

The block cache is shared across different tablets in a given YB-TServer, leading to highly-efficient memory utilization in cases when one tablet is read more often than others. For example, if one table has a read-heavy usage pattern compared to others, the block cache would automatically favor blocks of this table, as the block cache is global across all tablet peers.

## Space amplification

YugabyteDB's compactions are size-tiered. Size-tier compactions have the advantage of lower disk write (I/O) amplification when compared to level compactions. There may be a concern that size-tiered compactions have a higher space amplification (that it needs 50% space head room). This is not true in YugabyteDB because each table is broken into several tablets and concurrent compactions across tablets are throttled to a specific maximum. The typical space amplification in YugabyteDB tends to be in the 10-20% range.

## Throttled compactions

The compactions are throttled across tablets in a given YB-TServer to prevent compaction storms. This prevents, for example, high foreground latencies during a compaction storm.

The default policy ensures that doing a compaction is worthwhile. The algorithm tries to make sure that the files being compacted are not too disparate in terms of size. For example, it does not make sense to compact a 100GB file with a 1GB file to produce a 101GB file, because it would require a lot of unnecessary I/O for little gain.

## Small and large compaction queues

Compactions are prioritized into large and small compactions with some prioritization to keep the system functional even in extreme I/O patterns.

In addition to throttling controls for compactions, YugabyteDB does a variety of internal optimizations to minimize impact of compactions on foreground latencies. For example, a prioritized queue to give priority to small compactions over large compactions to make sure the number of SSTable files for any tablet stays as low as possible.

### Manual compactions

YugabyteDB allows compactions to be externally triggered on a table using the [`compact_table`](../../../admin/yb-admin/#compact-table) command in the [yb-admin utility](../../../admin/yb-admin/). This is useful when new data is no longer coming into the system for a table and you might want to reclaim disk space due to overwrites or deletes that have already happened, or due to TTL expiry.

### Statistics-based full compactions to improve read performance

YugabyteDB tracks the number of key-value pairs that are read at the DocDB level over a sliding period of time (dictated by the [auto_compact_stat_window_seconds](../../../reference/configuration/yb-tserver#auto-compact-stat-window-seconds) YB-TServer flag). If YugabyteDB detects an overwhelming amount of the DocDB reads in a tablet are skipping over tombstoned and obsolete keys, then a full compaction will be triggered to remove the unnecessary keys.

Once all of the following conditions are met in the sliding window, a full compaction is automatically triggered on the tablet:

- The ratio of obsolete (for example, deleted or removed due to TTL) versus active keys read reaches the threshold [auto_compact_percent_obsolete](../../../reference/configuration/yb-tserver/#auto-compact-percent-obsolete).

- Enough keys have been read ([auto_compact_min_obsolete_keys_found](../../../reference/configuration/yb-tserver/#auto-compact-min-obsolete-keys-found)).

While this feature is compatible with tables with TTL, YugabyteDB won't schedule compactions on tables with TTL if the [TTL file expiration](../../../develop/learn/ttl-data-expiration-ycql/#efficient-data-expiration-for-ttl) feature is active.

### Scheduled full compactions

 YugabyteDB allows full compactions over all data in a tablet to be scheduled automatically using the [scheduled_full_compaction_frequency_hours](../../../reference/configuration/yb-tserver#scheduled-full-compaction-frequency-hours) and [scheduled_full_compaction_jitter_factor_percentage](../../../reference/configuration/yb-tserver#scheduled-full-compaction-jitter-factor-percentage) YB-TServer flags. This can be useful for performance and disk space reclamation for workloads with a large number of overwrites or deletes on a regular basis. This can be used with tables with TTL as well, but is not compatible with the [TTL file expiration](../../../develop/learn/ttl-data-expiration-ycql/#efficient-data-expiration-for-ttl) feature.

### Server-global memstore limit

Server-global memstore limit tracks and enforces a global size across the memstores for different tablets. This is useful when there is a skew in the write rate across tablets. For example, if there are tablets belonging to multiple tables in a single YB-TServer and one of the tables gets a lot more writes than the other tables, the write-heavy table is allowed to grow much larger than it could if there was a per-tablet memory limit. This allows good write efficiency.

### Auto-sizing of block cache and memstore

The block cache and memstores represent some of the larger memory-consuming components. Since these are global across all the tablet-peers, this makes memory management and sizing of these components across a variety of workloads easy. Based on the RAM available on the system, the YB-TServer automatically gives a certain percentage of the total available memory to the block cache, and another percentage to memstores.

### Distributing tablet load uniformly across data disks

On multi-SSD machines, the data (SSTable) and WAL (Raft write-ahead log) for various tablets of tables are evenly distributed across the attached disks on a per-table basis. This load distribution (also known as striping), ensures that each disk handles an even amount of load for each table.
