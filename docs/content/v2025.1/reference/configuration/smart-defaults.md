---
title: Smart defaults
headerTitle: Smart defaults
linkTitle: Smart defaults
description: Smart defaults for YugabyteDB settings.
headContent: Set smart memory defaults when configuring YugabyteDB
menu:
  v2025.1:
    identifier: smart-defaults
    parent: configuration
    weight: 3100
type: docs
---

To configure a YugabyteDB deployment, you can use smart defaults to manage some performance features and guardrails. These defaults take into account the amount of RAM and cores available to optimize resources available to the cluster.

## Memory division smart defaults

Use the [use_memory_defaults_optimized_for_ysql](../yb-tserver/#use-memory-defaults-optimized-for-ysql) flag to automatically set defaults for how much memory is made available to YugabyteDB processes that run on the nodes of your cluster. These defaults take into account the amount of RAM and cores available, and are optimized for using YSQL.

When the flag is false, the defaults are more suitable for YCQL but do not take into account the amount of RAM and cores available.

### Split of memory between processes on nodes

If `use_memory_defaults_optimized_for_ysql` is true, then the [memory division flag](../yb-tserver/#memory-division-flags) defaults change to provide much more memory for PostgreSQL; furthermore, they optimize for the node size.

If these defaults are used for both TServer and Master, then a node's available memory is partitioned as follows:

| node RAM GiB (_M_): | _M_ &nbsp;&le;&nbsp; 4 | 4 < _M_ &nbsp;&le;&nbsp; 8 | 8 < _M_ &nbsp;&le;&nbsp; 16 | 16 < _M_ |
| :--- | ---: | ---: | ---: | ---: |
| TServer %  | 45% | 48% | 57% | 60% |
| Master %   | 20% | 15% | 10% | 10% |
| PostgreSQL % | 25% | 27% | 28% | 27% |
| other %    | 10% | 10% |  5% |  3% |

To read this table, take your node's available memory in GiB, call it _M_, and find the column who's heading condition _M_ meets.  For example, a node with 7 GiB of available memory would fall under the column labeled "4 < _M_ &le; 8" because 4 < 7 &le; 8.  The defaults for [--default_memory_limit_to_ram_ratio](../yb-tserver/#default-memory-limit-to-ram-ratio) on this node will thus be `0.48` for TServers and `0.15` for Masters. The PostgreSQL and other percentages are not set via a flag currently but rather consist of whatever memory is left after TServer and Master take their cut.  There is currently no distinction between PostgreSQL and other memory except on [YugabyteDB Aeon](/stable/yugabyte-cloud/) where a [cgroup](https://www.cybertec-postgresql.com/en/linux-cgroups-for-postgresql/) is used to limit the PostgreSQL memory.

For comparison, when `--use_memory_defaults_optimized_for_ysql` is `false`, the split is TServer 85%, Master 10%, PostgreSQL 0%, and other 5%.

### Split of memory within processes

Some defaults for the split of memory _within_ processes (as opposed to between processes on a node) are also affected by the `--use_memory_defaults_optimized_for_ysql` setting.

The defaults for the [split of memory _within_ a TServer](../yb-tserver/#flags-controlling-the-split-of-memory-within-a-tserver) that change when `--use_memory_defaults_optimized_for_ysql` is `true` do not depend on the node size, and are described in the following table:

| flag | default |
| :--- | :--- |
| --db_block_cache_size_percentage | 32 |
| --tablet_overhead_size_percentage | 10 |

The default value of `--db_block_cache_size_percentage` has been picked to avoid oversubscribing memory on the assumption that 10% of memory is reserved for per-tablet overhead. (Other TServer components and overhead from TCMalloc consume the remaining 58%.)

Currently, the defaults for the [split of memory _within_ a Master process](../yb-master/#flags-controlling-the-split-of-memory-within-a-master) do not depend on node size, and are not affected by the `--use_memory_defaults_optimized_for_ysql` setting. This could change in future releases.
