---
title: yb-tserver configuration reference
headerTitle: yb-tserver
linkTitle: yb-tserver
description: YugabyteDB Tablet Server (yb-tserver) binary and configuration flags to store and manage data for client applications.
menu:
  preview:
    identifier: yb-tserver
    parent: configuration
    weight: 2440
type: docs
---

Use the yb-tserver binary and its flags to configure the [YB-TServer](../../../architecture/yb-tserver/) server. The yb-tserver executable file is located in the `bin` directory of YugabyteDB home.

## Syntax

```sh
yb-tserver [ flags ]
```

### Example

```sh
./bin/yb-tserver \
--tserver_master_addrs 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
--rpc_bind_addresses 172.151.17.130 \
--enable_ysql \
--fs_data_dirs "/home/centos/disk1,/home/centos/disk2" &
```

### Online help

To display the online help, run `yb-tserver --help` from the YugabyteDB home directory:

```sh
./bin/yb-tserver --help
```

##### --help

Displays help on all flags.

##### --helpon

Displays help on modules named by the specified flag value.

## All flags

The following sections describe the flags considered relevant to configuring YugabyteDB for production deployments. For a list of all flags, see [All YB-TServer flags](../all-flags-yb-tserver/).

## General flags

##### --flagfile

Specifies the file to load the configuration flags from. The configuration flags must be in the same format as supported by the command line flags.

##### --version

Shows version and build info, then exits.

##### --tserver_master_addrs

Specifies a comma-separated list of all the yb-master RPC addresses.

Required.

Default: `127.0.0.1:7100`

The number of comma-separated values should match the total number of YB-Master servers (or the replication factor).

##### --fs_data_dirs

Specifies a comma-separated list of mount directories, where yb-tserver will add a `yb-data/tserver` data directory, `tserver.err`, `tserver.out`, and `pg_data` directory.

Required.

Changing the value of this flag after the cluster has already been created is not supported.

##### --fs_wal_dirs

Specifies a comma-separated list of directories, where yb-tserver will store write-ahead (WAL) logs. This can be the same as one of the directories listed in `--fs_data_dirs`, but not a subdirectory of a data directory.

Default: The same value as `--fs_data_dirs`

##### --max_clock_skew_usec

Specifies the expected maximum clock skew, in microseconds (µs), between any two nodes in your deployment.

Default: `500000` (500,000 µs = 500ms)

##### --rpc_bind_addresses

Specifies the comma-separated list of the network interface addresses to which to bind for RPC connections.

The values must match on all [yb-master](../yb-master/#rpc-bind-addresses) and yb-tserver configurations.

Default: Private IP address of the host on which the server is running, as defined in `/home/yugabyte/tserver/conf/server.conf`. For example:

```sh
egrep -i rpc /home/yugabyte/tserver/conf/server.conf
--rpc_bind_addresses=172.161.x.x:9100
```

Make sure that the [`server_broadcast_addresses`](#server-broadcast-addresses) flag is set correctly if the following applies:

- `rpc_bind_addresses` is set to `0.0.0.0`
- `rpc_bind_addresses` involves public IP addresses such as, for example, `0.0.0.0:9100`, which instructs the server to listen on all available network interfaces.

##### --server_broadcast_addresses

Specifies the public IP or DNS hostname of the server (with an optional port). This value is used by servers to communicate with one another, depending on the connection policy parameter.

Default: `""`

##### --tablet_server_svc_queue_length

Specifies the queue size for the tablet server to serve reads and writes from applications.

Default: `5000`

##### --dns_cache_expiration_ms

Specifies the duration, in milliseconds, until a cached DNS resolution expires. When hostnames are used instead of IP addresses, a DNS resolver must be queried to match hostnames to IP addresses. By using a local DNS cache to temporarily store DNS lookups, DNS queries can be resolved quicker and additional queries can be avoided, thereby reducing latency, improving load times, and reducing bandwidth and CPU consumption.

Default: `60000` (1 minute)

If you change this value from the default, be sure to add the identical value to all YB-Master and YB-TServer configurations.

##### --use_private_ip

Specifies the policy that determines when to use private IP addresses for inter-node communication. Possible values are `never`, `zone`, `cloud`, and `region`. Based on the values of the [geo-distribution flags](#geo-distribution-flags).

Valid values for the policy are:

- `never` — Always use [`--server_broadcast_addresses`](#server-broadcast-addresses).
- `zone` — Use the private IP inside a zone; use [`--server_broadcast_addresses`](#server-broadcast-addresses) outside the zone.
- `region` — Use the private IP address across all zone in a region; use [`--server_broadcast_addresses`](#server-broadcast-addresses) outside the region.

Default: `never`

##### --webserver_interface

The address to bind for the web server user interface.

Default: `0.0.0.0` (`127.0.0.1`)

##### --webserver_port

The port for monitoring the web server.

Default: `9000`

##### --webserver_doc_root

The monitoring web server home directory..

Default: The `www` directory in the YugabyteDB home directory.

##### --webserver_certificate_file

Location of the SSL certificate file (in .pem format) to use for the web server. If empty, SSL is not enabled for the web server.

Default: `""`

##### --webserver_authentication_domain

Domain used for `.htpasswd` authentication. This should be used in conjunction with [`--webserver_password_file`](#webserver-password-file).

Default: `""`

##### --webserver_password_file

Location of the `.htpasswd` file containing usernames and hashed passwords, for authentication to the web server.

Default: `""`

## Logging flags

##### --log_dir

The directory to write yb-tserver log files.

Default: The same as [`--fs_data_dirs`](#fs-data-dirs)

##### --logtostderr

Write log messages to `stderr` instead of `logfiles`.

Default: `false`

##### --max_log_size

The maximum log size, in megabytes (MB). A value of `0` will be silently overridden to `1`.

Default: `1800` (1.8 GB)

##### --minloglevel

The minimum level to log messages. Values are: `0` (INFO), `1`, `2`, `3` (FATAL).

Default: `0` (INFO)

##### --stderrthreshold

Log messages at, or above, this level are copied to `stderr` in addition to log files.

Default: `2`

##### --stop_logging_if_full_disk

Stop attempting to log to disk if the disk is full.

Default: `false`

##### --callhome_enabled

Disable callhome diagnostics.

Default: `true`


## Memory division flags

These flags are used to determine how the RAM of a node is split between
the [TServer](../../../architecture/key-concepts/#tserver) and other processes, including Postgres and a [master](../../../architecture/key-concepts/#master-server) process if present, as well as how to split memory inside of a TServer between various internal components like the RocksDB block cache.

{{< warning title="Warning" >}}

Ensure you do not _oversubscribe memory_ when changing these flags: make sure the amount of memory reserved for TServer and master if present leaves enough memory on the node for Postgres, and any required other processes like monitoring agents plus the memory needed by the kernel.

{{< /warning >}}


### Flags controlling the defaults for the other memory division flags

The memory division flags have multiple sets of defaults; which set of defaults is in force depends on these flags.  Note that these defaults can differ between TServer and master.

##### --use_memory_defaults_optimized_for_ysql

If true, the defaults for the memory division settings take into account the amount of RAM and cores available and are optimized for using YSQL.  If false, the defaults will be the old defaults, which are more suitable for YCQL but do not take into account the amount of RAM and cores available.

Default: `false`

If this flag is true then the memory division flag defaults change to provide much more memory for Postgres; furthermore, they optimize for the node size.

If these defaults are used for both TServer and master, then a node's available memory is partitioned as follows:

| node RAM GiB (_M_): | _M_ &nbsp;&le;&nbsp; 4 | 4 < _M_ &nbsp;&le;&nbsp; 8 | 8 < _M_ &nbsp;&le;&nbsp; 16 | 16 < _M_ |
| :--- | ---: | ---: | ---: | ---: |
| TServer %  | 45% | 48% | 57% | 60% |
| master %   | 20% | 15% | 10% | 10% |
| Postgres % | 25% | 27% | 28% | 27% |
| other %    | 10% | 10% |  5% |  3% |

To read this table, take your node's available memory in GiB, call it _M_, and find the column who's heading condition _M_ meets.  For example, a node with 7 GiB of available memory would fall under the column labeled "4 < _M_ &le; 8" because 4 < 7 &le; 8.  The defaults for [`--default_memory_limit_to_ram_ratio`](#default-memory-limit-to-ram-ratio) on this node will thus be `0.48` for TServers and `0.15` for masters. The Postgres and other percentages are not set via a flag currently but rather consist of whatever memory is left after TServer and master take their cut.  There is currently no distinction between Postgres and other memory except on [YugabyteDB Aeon](/preview/yugabyte-cloud/) where a [cgroup](https://www.cybertec-postgresql.com/en/linux-cgroups-for-postgresql/) is used to limit the Postgres memory.

For comparison, when `--use_memory_defaults_optimized_for_ysql` is `false`, the split is TServer 85%, master 10%, Postgres 0%, and other 5%.

The defaults for the master process partitioning flags when `--use_memory_defaults_optimized_for_ysql` is `true` do not depend on the node size, and are described in the following table:

| flag | default |
| :--- | :--- |
| --db_block_cache_size_percentage | 32 |
| --tablet_overhead_size_percentage | 10 |

The default value of [`--db_block_cache_size_percentage`](#db_block_cache_size_percentage) here has been picked to avoid oversubscribing memory on the assumption that 10% of memory is reserved for per-tablet overhead.  (Other TServer components and overhead from TCMalloc consume the remaining 58%.)

Given the amount of RAM devoted to per tablet overhead, it is possible to compute the maximum number of tablet replicas (see [allowing for tablet replica overheads](../../../develop/best-practices-ysql#allowing-for-tablet-replica-overheads)); following are some sample values for selected node sizes using `--use_memory_defaults_optimized_for_ysql`:

| total node GiB | max number of tablet replicas | max number of Postgres connections |
| ---: | ---: | ---: |
|   4 |    240 |  30 |
|   8 |    530 |  65 |
|  16 |  1,250 | 130 |
|  32 |  2,700 | 225 |
|  64 |  5,500 | 370 |
| 128 | 11,000 | 550 |
| 256 | 22,100 | 730 |

These values are approximate because different kernels use different amounts of memory, leaving different amounts of memory for the TServer and thus the per-tablet overhead TServer component.

Also shown is an estimate of how many Postgres connections that node can handle assuming default Postgres flags and usage.  Unusually memory expensive queries or preloading Postgres catalog information will reduce the number of connections that can be supported.

Thus a 8 GiB node would be expected to be able support 530 tablet replicas and 65 (physical) typical Postgres connections.  A universe of six of these nodes would be able to support 530 \* 2 = 1,060 [RF3](../../../architecture/key-concepts/#replication-factor-rf) tablets and 65 \* 6 = 570 typical physical Postgres connections assuming the connections are evenly distributed among the nodes.


### Flags controlling the split of memory among processes

Note that in general these flags will have different values for TServer and master processes.

##### --memory_limit_hard_bytes

Maximum amount of memory this process should use in bytes, that is, its hard memory limit.  A value of `0` specifies to instead use a percentage of the total system memory; see [`--default_memory_limit_to_ram_ratio`](#default-memory-limit-to-ram-ratio) for the percentage used.  A value of `-1` disables all memory limiting.

Default: `0`

##### --default_memory_limit_to_ram_ratio

The percentage of available RAM to use for this process if [`--memory_limit_hard_bytes`](#memory-limit-hard-bytes) is `0`.  The special value `-1000` means to instead use the default value for this flag.  Available RAM excludes memory reserved by the kernel.

Default: `0.85` unless [`--use_memory_defaults_optimized_for_ysql`](#use-memory-defaults-optimized-for-ysql) is true.


### Flags controlling the split of memory within a TServer

##### --db_block_cache_size_bytes

Size of the shared RocksDB block cache (in bytes).  A value of `-1` specifies to instead use a percentage of this processes' hard memory limit; see [`--db_block_cache_size_percentage`](#db-block-cache-size-percentage) for the percentage used.  A value of `-2` disables the block cache.

Default: `-1`

##### --db_block_cache_size_percentage

Percentage of the process' hard memory limit to use for the shared RocksDB block cache if [`--db_block_cache_size_bytes`](#db-block-cache-size-bytes) is `-1`.  The special value `-1000` means to instead use the default value for this flag.  The special value `-3` means to use an older default that does not take the amount of RAM into account.

Default: `50` unless [`--use_memory_defaults_optimized_for_ysql`](#use-memory-defaults-optimized-for-ysql) is true.

##### --tablet_overhead_size_percentage

Percentage of the process' hard memory limit to use for tablet-related overheads. A value of `0` means no limit.  Must be between `0` and `100` inclusive. Exception: `-1000` specifies to instead use the default value for this flag.

Each tablet replica generally requires 700 MiB of this memory.

Default: `0` unless [`--use_memory_defaults_optimized_for_ysql`](#use-memory-defaults-optimized-for-ysql) is true.

## Raft flags

For a typical deployment, values used for Raft and the write ahead log (WAL) flags in yb-tserver configurations should match the values in [yb-master](../yb-master/#raft-flags) configurations.

##### --follower_unavailable_considered_failed_sec

The duration, in seconds, after which a follower is considered to be failed because the leader has not received a heartbeat. The follower is then evicted from the configuration and the data is re-replicated elsewhere.

Default: `900` (15 minutes)

The `--follower_unavailable_considered_failed_sec` value should match the value for [`--log_min_seconds_to_retain`](#log-min-seconds-to-retain).

##### --evict_failed_followers

Failed followers will be evicted from the Raft group and the data will be re-replicated.

Default: `true`

##### --leader_failure_max_missed_heartbeat_periods

The maximum heartbeat periods that the leader can fail to heartbeat in before the leader is considered to be failed. The total failure timeout, in milliseconds (ms), is [`--raft_heartbeat_interval_ms`](#raft-heartbeat-interval-ms) multiplied by `--leader_failure_max_missed_heartbeat_periods`.

For read replica clusters, set the value to `10` in all yb-tserver and yb-master configurations.  Because the data is globally replicated, RPC latencies are higher. Use this flag to increase the failure detection interval in such a higher RPC latency deployment.

Default: `6`

##### --leader_lease_duration_ms

The leader lease duration, in milliseconds. A leader keeps establishing a new lease or extending the existing one with every consensus update. A new server is not allowed to serve as a leader (that is, serve up-to-date read requests or acknowledge write requests) until a lease of this duration has definitely expired on the old leader's side, or the old leader has explicitly acknowledged the new leader's lease.

This lease allows the leader to safely serve reads for the duration of its lease, even during a network partition. For more information, refer to [Leader leases](../../../architecture/transactions/single-row-transactions/#leader-leases-reading-the-latest-data-in-case-of-a-network-partition).

Leader lease duration should be longer than the heartbeat interval, and less than the multiple of `--leader_failure_max_missed_heartbeat_periods` multiplied by `--raft_heartbeat_interval_ms`.

Default: `2000`

##### --max_stale_read_bound_time_ms

Specifies the maximum bounded staleness (duration), in milliseconds, before a follower forwards a read request to the leader.

In a geo-distributed cluster, with followers located a long distance from the tablet leader, you can use this setting to increase the maximum bounded staleness.

Default: `10000` (10 seconds)

##### --raft_heartbeat_interval_ms

The heartbeat interval, in milliseconds (ms), for Raft replication. The leader produces heartbeats to followers at this interval. The followers expect a heartbeat at this interval and consider a leader to have failed if it misses several in a row.

Default: `500`

### Write ahead log (WAL) flags

Ensure that values used for the write ahead log (WAL) in yb-tserver configurations match the values for yb-master configurations.

##### --fs_wal_dirs

The directory where the yb-tserver retains WAL files. May be the same as one of the directories listed in [`--fs_data_dirs`](#fs-data-dirs), but not a subdirectory of a data directory.

Default: The same as `--fs_data_dirs`

##### --durable_wal_write

If set to `false`, the writes to the WAL are synchronized to disk every [`interval_durable_wal_write_ms`](#interval-durable-wal-write-ms) milliseconds (ms) or every [`bytes_durable_wal_write_mb`](#bytes-durable-wal-write-mb) megabyte (MB), whichever comes first. This default setting is recommended only for multi-AZ or multi-region deployments where the availability zones (AZs) or regions are independent failure domains and there is not a risk of correlated power loss. For single AZ deployments, this flag should be set to `true`.

Default: `false`

##### --interval_durable_wal_write_ms

When [`--durable_wal_write`](#durable-wal-write) is false, writes to the WAL are synced to disk every `--interval_durable_wal_write_ms` or [`--bytes_durable_wal_write_mb`](#bytes-durable-wal-write-mb), whichever comes first.

Default: `1000`

##### --bytes_durable_wal_write_mb

When [`--durable_wal_write`](#durable-wal-write) is `false`, writes to the WAL are synced to disk every `--bytes_durable_wal_write_mb` or `--interval_durable_wal_write_ms`, whichever comes first.

Default: `1`

##### --log_min_seconds_to_retain

The minimum duration, in seconds, to retain WAL segments, regardless of durability requirements. WAL segments can be retained for a longer amount of time, if they are necessary for correct restart. This value should be set long enough such that a tablet server which has temporarily failed can be restarted in the given time period.

Default: `900` (15 minutes)

##### --log_min_segments_to_retain

The minimum number of WAL segments (files) to retain, regardless of durability requirements. The value must be at least `1`.

Default: `2`

##### --log_segment_size_mb

The size, in megabytes (MB), of a WAL segment (file). When the WAL segment reaches the specified size, then a log rollover occurs and a new WAL segment file is created.

Default: `64`

##### --reuse_unclosed_segment_threshold_bytes

When the server restarts from a previous crash, if the tablet's last WAL file size is less than or equal to this threshold value, the last WAL file will be reused. Otherwise, WAL will allocate a new file at bootstrap. To disable WAL reuse, set the value to `-1`.

Default: The default value in `2.18.1` is `-1` - feature is disabled by default. The default value starting from `2.19.1` is `524288` (0.5 MB) - feature is enabled by default.

## Sharding flags

##### --yb_num_shards_per_tserver

The number of shards (tablets) per YB-TServer for each YCQL table when a user table is created.

Default: `-1`, where the number of shards is determined at runtime, as follows:

- If [enable_automatic_tablet_splitting](#enable-automatic-tablet-splitting) is `true`
  - The default value is considered as `1`.
  - For servers with 4 CPU cores or less, the number of tablets for each table doesn't depend on the number of YB-TServers. Instead, for 2 CPU cores or less, 1 tablet per cluster is created; for 4 CPU cores or less, 2 tablets per cluster are created.

- If `enable_automatic_tablet_splitting` is `false`
  - For servers with up to two CPU cores, the default value is considered as `4`.
  - For three or more CPU cores, the default value is considered as `8`.

Local cluster installations created using yb-ctl and yb-docker-ctl use a default value of `2` for this flag.

Clusters created using yugabyted always use a default value of `1`.

{{< note title="Note" >}}

- This value must match on all yb-master and yb-tserver configurations of a YugabyteDB cluster.
- If the value is set to *Default* (`-1`), then the system automatically determines an appropriate value based on the number of CPU cores and internally *updates* the flag with the intended value during startup prior to version 2.18 and the flag remains *unchanged* starting from version 2.18.
- The [`CREATE TABLE ... WITH TABLETS = <num>`](../../../api/ycql/ddl_create_table/#create-a-table-specifying-the-number-of-tablets) clause can be used on a per-table basis to override the `yb_num_shards_per_tserver` value.

{{< /note >}}

##### --ysql_num_shards_per_tserver

The number of shards (tablets) per YB-TServer for each YSQL table when a user table is created.

Default: `-1`, where the number of shards is determined at runtime, as follows:

- If [enable_automatic_tablet_splitting](#enable-automatic-tablet-splitting) is `true`
  - The default value is considered as `1`.
  - For servers with 4 CPU cores or less, the number of tablets for each table doesn't depend on the number of YB-TServers. Instead, for 2 CPU cores or less, 1 tablet per cluster is created; for 4 CPU cores or less, 2 tablets per cluster are created.

- If `enable_automatic_tablet_splitting` is `false`
  - For servers with up to two CPU cores, the default value is considered as `2`.
  - For servers with three or four CPU cores, the default value is considered as `4`.
  - Beyond four cores, the default value is considered as `8`.

Local cluster installations created using yb-ctl and yb-docker-ctl use a default value of `2` for this flag.

Clusters created using yugabyted always use a default value of `1`.

{{< note title="Note" >}}

- This value must match on all yb-master and yb-tserver configurations of a YugabyteDB cluster.
- If the value is set to *Default* (`-1`), the system automatically determines an appropriate value based on the number of CPU cores and internally *updates* the flag with the intended value during startup prior to version 2.18 and the flag remains *unchanged* starting from version 2.18.
- The [`CREATE TABLE ...SPLIT INTO`](../../../api/ysql/the-sql-language/statements/ddl_create_table/#split-into) clause can be used on a per-table basis to override the `ysql_num_shards_per_tserver` value.

{{< /note >}}

##### --cleanup_split_tablets_interval_sec

Interval at which the tablet manager tries to cleanup split tablets that are no longer needed. Setting this to 0 disables cleanup of split tablets.

Default: `60`

##### --enable_automatic_tablet_splitting

Enables YugabyteDB to [automatically split tablets](../../../architecture/docdb-sharding/tablet-splitting/#automatic-tablet-splitting).

Default: `true`

{{< note title="Important" >}}

This value must match on all yb-master and yb-tserver configurations of a YugabyteDB cluster.

{{< /note >}}

##### --post_split_trigger_compaction_pool_max_threads

Deprecated. Use `full_compaction_pool_max_threads`.

##### --post_split_trigger_compaction_pool_max_queue_size

Deprecated. Use `full_compaction_pool_max_queue_size`.

##### --full_compaction_pool_max_threads

The maximum number of threads allowed for non-admin full compactions. This includes post-split compactions (compactions that remove irrelevant data from new tablets after splits) and scheduled full compactions.

Default: `1`

##### --full_compaction_pool_max_queue_size

The maximum number of full compaction tasks that can be queued simultaneously. This includes post-split compactions (compactions that remove irrelevant data from new tablets after splits) and scheduled full compactions.

Default: `200`

##### --auto_compact_check_interval_sec

The interval at which the full compaction task will check for tablets eligible for compaction (both for the statistics-based full compaction and scheduled full compaction features). `0` indicates that the statistics-based full compactions feature is disabled.

Default: `60`

##### --auto_compact_stat_window_seconds

Window of time in seconds over which DocDB read statistics are analyzed for the purpose of triggering full compactions to improve read performance. Both `auto_compact_percent_obsolete` and `auto_compact_min_obsolete_keys_found` are evaluated over this period of time.

`auto_compact_stat_window_seconds` must be evaluated as a multiple of `auto_compact_check_interval_sec`, and will be rounded up to meet this constraint. For example, if `auto_compact_stat_window_seconds` is set to `100` and `auto_compact_check_interval_sec` is set to `60`, it will be rounded up to `120` at runtime.

Default: `300`

##### --auto_compact_percent_obsolete

The percentage of obsolete keys (over total keys) read over the `auto_compact_stat_window_seconds` window of time required to trigger an automatic full compaction on a tablet. Only keys that are past their history retention (and thus can be garbage collected) are counted towards this threshold.

For example, if the flag is set to `99` and 100000 keys are read over that window of time, and 99900 of those are obsolete and past their history retention, a full compaction will be triggered (subject to other conditions).

Default: `99`

##### --auto_compact_min_obsolete_keys_found

Minimum number of keys that must be read over the last `auto_compact_stat_window_seconds` to trigger a statistics-based full compaction.

Default: `10000`

##### --auto_compact_min_wait_between_seconds

Minimum wait time between statistics-based and scheduled full compactions. To be used if statistics-based compactions are triggering too frequently.

Default: `0`

##### --scheduled_full_compaction_frequency_hours

The frequency with which full compactions should be scheduled on tablets. `0` indicates that the feature is disabled. Recommended value: `720` hours or greater (that is, 30 days).

Default: `0`

##### --scheduled_full_compaction_jitter_factor_percentage

Percentage of `scheduled_full_compaction_frequency_hours` to be used as jitter when determining full compaction schedule per tablet. Must be a value between `0` and `100`. Jitter is introduced to prevent many tablets from being scheduled for full compactions at the same time.

Jitter is deterministically computed when scheduling a compaction, between 0 and (frequency * jitter factor) hours. Once computed, the jitter is subtracted from the intended compaction frequency to determine the tablet's next compaction time.

Example: If `scheduled_full_compaction_frequency_hours` is `720` hours (that is, 30 days), and `scheduled_full_compaction_jitter_factor_percentage` is `33` percent, each tablet will be scheduled for compaction every `482` hours to `720` hours.

Default: `33`

##### --automatic_compaction_extra_priority

Assigns an extra priority to automatic (minor) compactions when automatic tablet splitting is enabled. This deprioritizes post-split compactions and ensures that smaller compactions are not starved. Suggested values are between 0 and 50.

Default: `50`

##### --ysql_colocate_database_by_default

When enabled, all databases created in the cluster are colocated by default. If you enable the flag after creating a cluster, you need to restart the YB-Master and YB-TServer services.

For more details, see [clusters in colocated tables](../../../architecture/docdb-sharding/colocated-tables/#clusters).

Default: `false`

##### tablet_replicas_per_core_limit

The number of tablet replicas that each core on a YB-TServer can support.

Default: `0` for no limit.

##### tablet_replicas_per_gib_limit

The number of tablet replicas that each GiB reserved by YB-TServers for tablet overheads can support.

Default: 1024 * (7/10) (corresponding to an overhead of roughly 700 KiB per tablet)

## Geo-distribution flags

Settings related to managing geo-distributed clusters:

##### --placement_zone

The name of the availability zone, or rack, where this instance is deployed.

{{<tip title="Rack awareness">}}
For on-premises deployments, consider racks as zones to treat them as fault domains.
{{</tip>}}

Default: `rack1`

##### --placement_region

Specifies the name of the region, or data center, where this instance is deployed.

Default: `datacenter1`

##### --placement_cloud

Specifies the name of the cloud where this instance is deployed.

Default: `cloud1`

##### --placement_uuid

The unique identifier for the cluster.

Default: `""`

##### --force_global_transactions

If true, forces all transactions through this instance to always be global transactions that use the `system.transactions` transaction status table. This is equivalent to always setting the YSQL parameter `force_global_transaction = TRUE`.

{{< note title="Global transaction latency" >}}

Avoid setting this flag when possible. All distributed transactions _can_ run without issue as global transactions, but you may have significantly higher latency when committing transactions, because YugabyteDB must achieve consensus across multiple regions to write to `system.transactions`. When necessary, it is preferable to selectively set the YSQL parameter `force_global_transaction = TRUE` rather than setting this flag.

{{< /note >}}

Default: `false`

##### --auto-create-local-transaction-tables

If true, transaction status tables will be created under each YSQL tablespace that has a placement set and contains at least one other table.

Default: `true`

##### --auto-promote-nonlocal-transactions-to-global

If true, local transactions using transaction status tables other than `system.transactions` will be automatically promoted to global transactions using the `system.transactions` transaction status table upon accessing data outside of the local region.

Default: `true`

## xCluster flags

Settings related to managing xClusters.

##### --xcluster_svc_queue_size

The RPC queue size of the xCluster service. Should match the size of [tablet_server_svc_queue_length](#tablet-server-svc-queue-length) used for read and write requests.

Default: `5000`

## API flags

### YSQL

The following flags support the use of the [YSQL API](../../../api/ysql/):

##### --enable_ysql

Enables the YSQL API.

Default: `true`

Ensure that `enable_ysql` values in yb-tserver configurations match the values in yb-master configurations.

##### --ysql_enable_auth

Enables YSQL authentication.

When YSQL authentication is enabled, you can sign into `ysqlsh` using the default `yugabyte` user that has a default password of `yugabyte`.

Default: `false`

##### --ysql_enable_profile

Enables YSQL [login profiles](../../../secure/enable-authentication/ysql-login-profiles/).

When YSQL login profiles are enabled, you can set limits on the number of failed login attempts made by users.

Default: `false`

##### --pgsql_proxy_bind_address

Specifies the TCP/IP bind addresses for the YSQL API. The default value of `0.0.0.0:5433` allows listening for all IPv4 addresses access to localhost on port `5433`. The `--pgsql_proxy_bind_address` value overwrites `listen_addresses` (default value of `127.0.0.1:5433`) that controls which interfaces accept connection attempts.

To specify fine-grained access control over who can access the server, use [`--ysql_hba_conf`](#ysql-hba-conf).

Default: `0.0.0.0:5433`

##### --pgsql_proxy_webserver_port

Specifies the web server port for YSQL metrics monitoring.

Default: `13000`

##### --ysql_hba_conf

Deprecated. Use `--ysql_hba_conf_csv` instead.

##### --ysql_hba_conf_csv

Specifies a comma-separated list of PostgreSQL client authentication settings that is written to the `ysql_hba.conf` file. To see the current values in the `ysql_hba.conf` file, run the `SHOW hba_file;` statement and then view the file. Because the file is autogenerated, direct edits are overwritten by the autogenerated content.

For details on using `--ysql_hba_conf_csv` to specify client authentication, refer to [Host-based authentication](../../../secure/authentication/host-based-authentication/).

If a setting includes a comma (`,`) or double quotes (`"`), the setting should be enclosed in double quotes. In addition, double quotes inside the quoted text should be doubled (`""`).

Suppose you have two fields: `host all all 127.0.0.1/0 password` and `host all all 0.0.0.0/0 ldap ldapserver=***** ldapsearchattribute=cn ldapport=3268 ldapbinddn=***** ldapbindpasswd="*****"`.

Because the second field includes double quotes (`"`) around one of the settings, you need to double the quotes and enclose the entire field in quotes, as follows:

```sh
"host all all 0.0.0.0/0 ldap ldapserver=***** ldapsearchattribute=cn ldapport=3268 ldapbinddn=***** ldapbindpasswd=""*****"""
```

To set the flag, join the fields using a comma (`,`) and enclose the final flag value in single quotes (`'`):

```sh
--ysql_hba_conf_csv='host all all 127.0.0.1/0 password,"host all all 0.0.0.0/0 ldap ldapserver=***** ldapsearchattribute=cn ldapport=3268 ldapbinddn=***** ldapbindpasswd=""*****"""'
```

Default: `"host all all 0.0.0.0/0 trust,host all all ::0/0 trust"`

##### --ysql_pg_conf

Deprecated. Use `--ysql_pg_conf_csv` instead.

##### --ysql_pg_conf_csv

Comma-separated list of PostgreSQL server configuration parameters that is appended to the `postgresql.conf` file. If internal quotation marks are required, surround each configuration pair having single quotation marks with double quotation marks.

For example:

```sh
--ysql_pg_conf_csv="log_line_prefix='%m [%p %l %c] %q[%C %R %Z %H] [%r %a %u %d] '","pgaudit.log='all, -misc'",pgaudit.log_parameter=on,pgaudit.log_relation=on,pgaudit.log_catalog=off,suppress_nonpg_logs=on
```

For information on available PostgreSQL server configuration parameters, refer to [Server Configuration](https://www.postgresql.org/docs/11/runtime-config.html) in the PostgreSQL documentation.

The server configuration parameters for YugabyteDB are the same as for PostgreSQL, with some minor exceptions. Refer to [PostgreSQL server options](#postgresql-server-options).

##### --ysql_timezone

Specifies the time zone for displaying and interpreting timestamps.

Default: Uses the YSQL time zone.

##### --ysql_datestyle

Specifies the display format for data and time values.

Default: Uses the YSQL display format.

##### --ysql_max_connections

Specifies the maximum number of concurrent YSQL connections per node.

This is a maximum per server, so a 3-node cluster will have a default of 900 available connections, globally.

Any active, idle in transaction, or idle in session connection counts toward the connection limit.

Some connections are reserved for superusers. The total number of superuser connections is determined by the `superuser_reserved_connections` [PostgreSQL server parameter](#postgresql-server-options). Connections available to non-superusers is equal to `ysql_max_connections` - `superuser_reserved_connections`.

Default: If `ysql_max_connections` is not set, the database startup process will determine the highest number of connections the system can support, from a minimum of 50 to a maximum of 300 (per node).

##### --ysql_default_transaction_isolation

Specifies the default transaction isolation level.

Valid values: `SERIALIZABLE`, `REPEATABLE READ`, `READ COMMITTED`, and `READ UNCOMMITTED`.

Default: `READ COMMITTED` {{<badge/ea>}}

Read Committed support is currently in [Early Access](/preview/releases/versioning/#feature-maturity). Read Committed Isolation is supported only if the YB-TServer flag `yb_enable_read_committed_isolation` is set to `true`. By default this flag is `false` and in this case the Read Committed isolation level of the YugabyteDB transactional layer falls back to the stricter Snapshot Isolation (in which case `READ COMMITTED` and `READ UNCOMMITTED` of YSQL also in turn use Snapshot Isolation).

##### --ysql_disable_index_backfill

Set this flag to `false` to enable online index backfill. When set to `false`, online index builds run while online, without failing other concurrent writes and traffic.

For details on how online index backfill works, see the [Online Index Backfill](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md) design document.

Default: `false`

##### --ysql_sequence_cache_method

Specifies where to cache sequence values.

Valid values are `connection` and `server`.

This flag requires the YB-TServer `yb_enable_sequence_pushdown` flag to be true (the default). Otherwise, the default behavior will occur regardless of this flag's value.

For details on caching values on the server and switching between cache methods, see the semantics on the [nextval](../../../api/ysql/exprs/func_nextval/) page.

Default: `connection`

##### --ysql_sequence_cache_minval

Specifies the minimum number of sequence values to cache in the client for every sequence object.

To turn off the default size of cache flag, set the flag to `0`.

For details on the expected behaviour when used with the sequence cache clause, see the semantics under [CREATE SEQUENCE](../../../api/ysql/the-sql-language/statements/ddl_create_sequence/#cache-cache) and [ALTER SEQUENCE](../../../api/ysql/the-sql-language/statements/ddl_alter_sequence/#cache-cache) pages.

Default: `100`

##### --ysql_yb_fetch_size_limit

{{<badge/tp>}} Specifies the maximum size (in bytes) of total data returned in one response when the query layer fetches rows of a table from DocDB. Used to bound how many rows can be returned in one request. Set to 0 to have no size limit.

You can also specify the value as a string. For example, you can set it to `'10MB'`.

You should have at least one of row limit or size limit set.

If both `--ysql_yb_fetch_row_limit` and `--ysql_yb_fetch_size_limit` are greater than zero, then limit is taken as the lower bound of the two values.

See also the [yb_fetch_size_limit](#yb-fetch-size-limit) configuration parameter. If both flag and parameter are set, the parameter takes precedence.

Default: 0

##### --ysql_yb_fetch_row_limit

{{<badge/tp>}} Specifies the maximum number of rows returned in one response when the query layer fetches rows of a table from DocDB. Used to bound how many rows can be returned in one request. Set to 0 to have no row limit.

You should have at least one of row limit or size limit set.

If both `--ysql_yb_fetch_row_limit` and `--ysql_yb_fetch_size_limit` are greater than zero, then limit is taken as the lower bound of the two values.

See also the [yb_fetch_row_limit](#yb-fetch-row-limit) configuration parameter. If both flag and parameter are set, the parameter takes precedence.

Default: 1024

##### --ysql_log_statement

Specifies the types of YSQL statements that should be logged.

Valid values: `none` (off), `ddl` (only data definition queries, such as create/alter/drop), `mod` (all modifying/write statements, includes DDLs plus insert/update/delete/truncate, etc), and `all` (all statements).

Default: `none`

##### --ysql_log_min_duration_statement

Logs the duration of each completed SQL statement that runs the specified duration (in milliseconds) or longer. Setting the value to `0` prints all statement durations. You can use this flag to help track down unoptimized (or "slow") queries.

Default: `-1` (disables logging statement durations)

##### --ysql_log_min_messages

Specifies the lowest YSQL message level to log.

##### --ysql_cron_database_name

Specifies the database where pg_cron is to be installed. You can create the database after setting the flag.

The [pg_cron extension](../../../explore/ysql-language-features/pg-extensions/extension-pgcron/) is installed on only one database (by default, `yugabyte`).

To change the database after the extension is created, you must first drop the extension and then change the flag value.

##### --ysql_output_buffer_size

Size of YSQL layer output buffer, in bytes. YSQL buffers query responses in this output buffer until either a buffer flush is requested by the client or the buffer overflows.

As long as no data has been flushed from the buffer, the database can retry queries on retryable errors. For example, you can increase the size of the buffer so that YSQL can retry [read restart errors](../../../architecture/transactions/read-restart-error).

Default: `262144` (256kB, type: int32)

##### --ysql_yb_bnl_batch_size

Sets the size of a tuple batch that's taken from the outer side of a [batched nested loop (BNL) join](../../../explore/ysql-language-features/join-strategies/#batched-nested-loop-join-bnl). When set to 1, BNLs are effectively turned off and won't be considered as a query plan candidate.

See also the [yb_bnl_batch_size](#yb-bnl-batch-size) configuration parameter. If both flag and parameter are set, the parameter takes precedence.

Default: 1024

### YCQL

The following flags support the use of the [YCQL API](../../../api/ycql/):

##### --use_cassandra_authentication

Specify `true` to enable YCQL authentication (`username` and `password`), enable YCQL security statements (`CREATE ROLE`, `DROP ROLE`, `GRANT ROLE`, `REVOKE ROLE`, `GRANT PERMISSION`, and `REVOKE PERMISSION`), and enforce permissions for YCQL statements.

Default: `false`

##### --cql_proxy_bind_address

Specifies the bind address for the YCQL API.

Default: `0.0.0.0:9042` (`127.0.0.1:9042`)

##### --cql_proxy_webserver_port

Specifies the port for monitoring YCQL metrics.

Default: `12000`

##### --cql_table_is_transactional_by_default

Specifies if YCQL tables are created with transactions enabled by default.

Default: `false`

##### --ycql_disable_index_backfill

Set this flag to `false` to enable online index backfill. When set to `false`, online index builds run while online, without failing other concurrent writes and traffic.

For details on how online index backfill works, see the [Online Index Backfill](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md) design document.

Default: `true`

##### --ycql_require_drop_privs_for_truncate

Set this flag to `true` to reject [`TRUNCATE`](../../../api/ycql/dml_truncate) statements unless allowed by [`DROP TABLE`](../../../api/ycql/ddl_drop_table) privileges.

Default: `false`

##### --ycql_enable_audit_log

Set this flag to `true` to enable audit logging for the universe.

For details, see [Audit logging for the YCQL API](../../../secure/audit-logging/audit-logging-ycql).

##### --ycql_allow_non_authenticated_password_reset

Set this flag to `true` to enable a superuser to reset a password.

Default: `false`

Note that to enable the password reset feature, you must first set the [`use_cassandra_authentication`](#use-cassandra-authentication) flag to false.

## Performance flags

Use the following two flags to select the SSTable compression type:

##### --enable_ondisk_compression

Enable SSTable compression at the cluster level.

Default: `true`

##### --compression_type

Change the SSTable compression type. The valid compression types are `Snappy`, `Zlib`, `LZ4`, and `NoCompression`.

Default: `Snappy`

If you select an invalid option, the cluster will not come up.

If you change this flag, the change takes effect after you restart the cluster nodes.

Changing this flag on an existing database is supported; a tablet can validly have SSTs with different compression types. Eventually, compaction will remove the old compression type files.

##### --regular_tablets_data_block_key_value_encoding

Key-value encoding to use for regular data blocks in RocksDB. Possible options: `shared_prefix`, `three_shared_parts`.

Default: `shared_prefix`

Only change this flag to `three_shared_parts` after you migrate the whole cluster to the YugabyteDB version that supports it.

##### --rocksdb_compact_flush_rate_limit_bytes_per_sec

Used to control rate of memstore flush and SSTable file compaction.

Default: `1GB` (1 GB/second)

##### --rocksdb_universal_compaction_min_merge_width

Compactions run only if there are at least `rocksdb_universal_compaction_min_merge_width` eligible files and their running total (summation of size of files considered so far) is within `rocksdb_universal_compaction_size_ratio` of the next file in consideration to be included into the same compaction.

Default: `4`

##### --rocksdb_max_background_compactions

Maximum number of threads to do background compactions (used when compactions need to catch up.) Unless `rocksdb_disable_compactions=true`, this cannot be set to zero.

Default: `-1`, where the value is calculated at runtime as follows:

- For servers with up to 4 CPU cores, the default value is considered as `1`.
- For servers with up to 8 CPU cores, the default value is considered as `2`.
- For servers with up to 32 CPU cores, the default value is considered as `3`.
- Beyond 32 cores, the default value is considered as `4`.

##### --rocksdb_compaction_size_threshold_bytes

Threshold beyond which a compaction is considered large.

Default: `2GB`

##### --rocksdb_level0_file_num_compaction_trigger

Number of files to trigger level-0 compaction. Set to `-1` if compaction should not be triggered by number of files at all.

Default: `5`.

##### --rocksdb_universal_compaction_size_ratio

Compactions run only if there are at least `rocksdb_universal_compaction_min_merge_width` eligible files and their running total (summation of size of files considered so far) is within `rocksdb_universal_compaction_size_ratio` of the next file in consideration to be included into the same compaction.

Default: `20`

##### --timestamp_history_retention_interval_sec

The time interval, in seconds, to retain history/older versions of data. Point-in-time reads at a hybrid time prior to this interval might not be allowed after a compaction and return a `Snapshot too old` error. Set this to be greater than the expected maximum duration of any single transaction in your application.

Default: `900` (15 minutes)

##### --remote_bootstrap_rate_limit_bytes_per_sec

Rate control across all tablets being remote bootstrapped from or to this process.

Default: `256MB` (256 MB/second)

##### --remote_bootstrap_from_leader_only

Based on the value (`true`/`false`) of the flag, the leader decides whether to instruct the new peer to attempt bootstrap from a closest caught-up peer. The leader too could be the closest peer depending on the new peer's geographic placement. Setting the flag to false will enable the feature of remote bootstrapping from a closest caught-up peer. The number of bootstrap attempts from a non-leader peer is limited by the flag [max_remote_bootstrap_attempts_from_non_leader](#max-remote-bootstrap-attempts-from-non-leader).

Default: `false`

{{< note title="Note" >}}

The code for the feature is present from version 2.16 and later, and can be enabled explicitly if needed. Starting from version 2.19, the feature is on by default.

{{< /note >}}

##### --max_remote_bootstrap_attempts_from_non_leader

When the flag [remote_bootstrap_from_leader_only](#remote-bootstrap-from-leader-only) is set to `false` (enabling the feature of bootstrapping from a closest peer), the number of attempts where the new peer tries to bootstrap from a non-leader peer is limited by the flag. After these failed bootstrap attempts for the new peer, the leader peer sets itself as the bootstrap source.

Default: `5`

##### --db_block_cache_num_shard_bits

Number of bits to use for sharding the block cache. The maximum permissible value is 19.

Default: `-1` (indicates a dynamic scheme that evaluates to 4 if number of cores is less than or equal to 16, 5 for 17-32 cores, 6 for 33-64 cores, and so on.)

{{< note title="Note" >}}

Starting from version 2.18, the default is `-1`. Previously it was `4`.

{{< /note >}}

## Network compression flags

Use the following two flags to configure RPC compression:

##### --enable_stream_compression

Controls whether YugabyteDB uses RPC compression.

Default: `true`

##### --stream_compression_algo

Specifies which RPC compression algorithm to use. Requires `enable_stream_compression` to be set to true. Valid values are:

0: No compression (default value)

1: Gzip

2: Snappy

3: LZ4

In most cases, LZ4 (`--stream_compression_algo=3`) offers the best compromise of compression performance versus CPU overhead.

To upgrade from an older version that doesn't support RPC compression (such as 2.4), to a newer version that does (such as 2.6), you need to do the following:

- Rolling restart to upgrade YugabyteDB to a version that supports compression.

- Rolling restart to enable compression, on both YB-Master and YB-TServer, by setting `enable_stream_compression=true`.

  Note that you can omit this step if the YugabyteDB version you are upgrading to already has compression enabled by default. For the stable release series, versions from 2.6.3.0 and later (including all 2.8 releases) have `enable_stream_compression` set to true by default. For the preview release series, this is all releases beyond 2.9.0.

- Rolling restart to set the compression algorithm to use, on both YB-Master and YB-TServer, such as by setting `stream_compression_algo=3`.

## Security flags

For details on enabling client-server encryption, see [Client-server encryption](../../../secure/tls-encryption/client-to-server/).

##### --certs_dir

Directory that contains certificate authority, private key, and certificates for this server.

Default: `""` (Uses `<data drive>/yb-data/tserver/data/certs`.)

##### --allow_insecure_connections

Allow insecure connections. Set to `false` to prevent any process with unencrypted communication from joining a cluster. Note that this flag requires the [`use_node_to_node_encryption`](#use-node-to-node-encryption) to be enabled and [`use_client_to_server_encryption`](#use-client-to-server-encryption) to be enabled.

Default: `true`

##### --certs_for_client_dir

The directory that contains certificate authority, private key, and certificates for this server that should be used for client-to-server communications.

Default: `""` (Use the same directory as for server-to-server communications.)

##### --dump_certificate_entries

Adds certificate entries, including IP addresses and hostnames, to log for handshake error messages. Enable this flag to debug certificate issues.

Default: `false`

##### --use_client_to_server_encryption

Use client-to-server, or client-server, encryption with YCQL.

Default: `false`

##### --use_node_to_node_encryption

Enable server-server or node-to-node encryption between YugabyteDB YB-Master and YB-TServer servers in a cluster or universe. To work properly, all YB-Master servers must also have their [`--use_node_to_node_encryption`](../yb-master/#use-node-to-node-encryption) setting enabled. When enabled, then [`--allow_insecure_connections`](#allow-insecure-connections) must be disabled.

Default: `false`

##### --cipher_list

Specify cipher lists for TLS 1.2 and below. (For TLS 1.3, use [--ciphersuite](#ciphersuite).) Use a colon (":") separated list of TLSv1.2 cipher names in order of preference. Use an exclamation mark ("!") to exclude ciphers. For example:

```sh
--cipher_list DEFAULTS:!DES:!IDEA:!3DES:!RC2
```

This allows all ciphers for TLS 1.2 to be accepted, except those matching the category of ciphers omitted.

This flag requires a restart or rolling restart.

Default: `DEFAULTS`

For more information, refer to [SSL_CTX_set_cipher_list](https://www.openssl.org/docs/man1.1.1/man3/SSL_CTX_set_cipher_list.html) in the OpenSSL documentation.

##### --ciphersuite

Specify cipher lists for TLS 1.3. (For TLS 1.2 and below, use [--cipher_list](#cipher-list).)

Use a colon (":") separated list of TLSv1.3 ciphersuite names in order of preference. Use an exclamation mark ("!") to exclude ciphers. For example:

```sh
--ciphersuite DEFAULTS:!CHACHA20
```

This allows all ciphersuites for TLS 1.3 to be accepted, except CHACHA20 ciphers.

This flag requires a restart or rolling restart.

Default: `DEFAULTS`

For more information, refer to [SSL_CTX_set_cipher_list](https://www.openssl.org/docs/man1.1.1/man3/SSL_CTX_set_cipher_list.html) in the OpenSSL documentation.

##### --ssl_protocols

Specifies an explicit allow-list of TLS protocols for YugabyteDB's internal RPC communication.

Default: An empty string, which is equivalent to allowing all protocols except "ssl2" and "ssl3".

You can pass a comma-separated list of strings, where the strings can be one of "ssl2", "ssl3", "tls10", "tls11", "tls12", and "tls13".

You can set the TLS version for node-to-node and client-node communication. To enforce TLS 1.2, set the flag to tls12 as follows:

```sh
--ssl_protocols = tls12
```

To specify a _minimum_ TLS version of 1.2, for example, the flag needs to be set to tls12, tls13, and all available subsequent versions.

```sh
--ssl_protocols = tls12,tls13
```

In addition, as this setting does not propagate to PostgreSQL, it is recommended that you specify the minimum TLS version (`ssl_min_protocol_version`) for PostgreSQL by setting the [`ysql_pg_conf_csv`](#ysql-pg-conf-csv) flag as follows:

```sh
--ysql_pg_conf_csv="ssl_min_protocol_version='TLSv1.2'"
```

## Packed row flags

The packed row format for the YSQL API is {{<badge/ga>}} as of v2.20.0, and for the YCQL API is {{<badge/tp>}}.

To learn about the packed row feature, see [Packed rows in DocDB](../../../architecture/docdb/packed-rows) in the architecture section.

##### --ysql_enable_packed_row

Whether packed row is enabled for YSQL.

Default: `true`

Packed Row for YSQL can be used from version 2.16.4 in production environments if the cluster is not used in xCluster settings. For xCluster scenarios, use version 2.18.1 and later. Starting from version 2.19 and later, the flag default is true for new clusters.

##### --ysql_enable_packed_row_for_colocated_table

Whether packed row is enabled for colocated tables in YSQL. The colocated table has an additional flag to mitigate [#15143](https://github.com/yugabyte/yugabyte-db/issues/15143).

Default: `false`

##### --ysql_packed_row_size_limit

Packed row size limit for YSQL. The default value is 0 (use block size as limit). For rows that are over this size limit, a greedy approach will be used to pack as many columns as possible, with the remaining columns stored as individual key-value pairs.

Default: `0`

##### --ycql_enable_packed_row

YCQL packed row support is currently in [Tech Preview](/preview/releases/versioning/#feature-maturity).

Whether packed row is enabled for YCQL.

Default: `false`

##### --ycql_packed_row_size_limit

Packed row size limit for YCQL. The default value is 0 (use block size as limit). For rows that are over this size limit, a greedy approach will be used to pack as many columns as possible, with the remaining columns stored as individual key-value pairs.

Default: `0`

## Change data capture (CDC) flags

To learn about CDC, see [Change data capture (CDC)](../../../architecture/docdb-replication/change-data-capture/).

##### --yb_enable_cdc_consistent_snapshot_streams

Support for creating a stream for Transactional CDC is currently in [Tech Preview](/preview/releases/versioning/#feature-maturity).

Enable support for creating streams for transactional CDC.

Default: `false`

##### --cdc_state_checkpoint_update_interval_ms

The rate at which CDC state's checkpoint is updated.

Default: `15000`

##### --cdc_ybclient_reactor_threads

The number of reactor threads to be used for processing `ybclient` requests for CDC. Increase to improve throughput on large tablet setups.

Default: `50`

##### --cdc_max_stream_intent_records

Maximum number of intent records allowed in a single CDC batch.

Default: `1000`

##### --cdc_snapshot_batch_size

Number of records fetched in a single batch of snapshot operation of CDC.

Default: `250`

##### --cdc_min_replicated_index_considered_stale_secs

If `cdc_min_replicated_index` hasn't been replicated in this amount of time, we reset its value to max int64 to avoid retaining any logs.

Default: `900` (15 minutes)

##### --timestamp_history_retention_interval_sec

Time interval (in seconds) to retain history or older versions of data.

Default: `900` (15 minutes)

##### --update_min_cdc_indices_interval_secs

How often to read the `cdc_state` table to get the minimum applied index for each tablet across all streams. This information is used to correctly keep log files that contain unapplied entries. This is also the rate at which a tablet's minimum replicated index across all streams is sent to the other peers in the configuration. If flag `enable_log_retention_by_op_idx` (default: `true`) is disabled, this flag has no effect.

Default: `60`

##### --cdc_checkpoint_opid_interval_ms

The number of seconds for which the client can go down and the intents will be retained. This means that if a client has not updated the checkpoint for this interval, the intents would be garbage collected.

Default: `60000`

{{< warning title="Warning" >}}

If you are using multiple streams, it is advised that you set this flag to `1800000` (30 minutes).

{{< /warning >}}

##### --log_max_seconds_to_retain

Number of seconds to retain log files. Log files older than this value will be deleted even if they contain unreplicated CDC entries. If 0, this flag will be ignored. This flag is ignored if a log segment contains entries that haven't been flushed to RocksDB.

Default: `86400`

##### --log_stop_retaining_min_disk_mb

Stop retaining logs if the space available for the logs falls below this limit, specified in megabytes. As with `log_max_seconds_to_retain`, this flag is ignored if a log segment contains unflushed entries.

Default: `102400`

##### --cdc_intent_retention_ms

The time period, in milliseconds, after which the intents will be cleaned up if there is no client polling for the change records.

Default: `14400000` (4 hours)

##### --cdcsdk_table_processing_limit_per_run

Number of tables to be added to the stream ID per run of the background thread which adds newly created tables to the active streams on its namespace.

Default: `2`

The following set of flags are only relevant for CDC using the PostgreSQL replication protocol. To learn about CDC using the PostgreSQL replication protocol, see [CDC using logical replication](../../../architecture/docdb-replication/cdc-logical-replication).

##### --ysql_yb_default_replica_identity

The default replica identity to be assigned to user defined tables at the time of creation. The flag is case sensitive and can take only one of the four possible values, `FULL`, `DEFAULT`,`'NOTHING` and `CHANGE`.

Default: `CHANGE`

##### --cdcsdk_enable_dynamic_table_support

Tables created after the creation of a replication slot are referred as Dynamic tables. This preview flag can be used to switch the dynamic addition of tables to the publication ON or OFF.

Default: `false`

##### --cdcsdk_publication_list_refresh_interval_secs

Interval in seconds at which the table list in the publication will be refreshed.

Default: `3600`

##### --cdcsdk_max_consistent_records

Controls the maximum number of records sent from Virtual WAL (VWAL) to walsender in consistent order.

Default: `500`

##### --cdcsdk_vwal_getchanges_resp_max_size_bytes

Max size (in bytes) of changes sent from CDC Service to [Virtual WAL](../../../architecture/docdb-replication/cdc-logical-replication)(VWAL) for a particular tablet.

Default: `1 MB`

##### --ysql_cdc_active_replication_slot_window_ms

Determines the window in milliseconds in which if a client has consumed the changes of a ReplicationSlot across any tablet, then it is considered to be actively used. ReplicationSlots which haven't been used in this interval are considered to be inactive.

Default: `60000`

## File expiration based on TTL flags

##### --tablet_enable_ttl_file_filter

Turn on the file expiration for TTL feature.

Default: `false`

##### --rocksdb_max_file_size_for_compaction

For tables with a `default_time_to_live` table property, sets a size threshold at which files will no longer be considered for compaction. Files over this threshold will still be considered for expiration. Disabled if value is `0`.

Ideally, `rocksdb_max_file_size_for_compaction` should strike a balance between expiring data at a reasonable frequency and not creating too many SST files (which can impact read performance). For instance, if 90 days worth of data is stored, consider setting this flag to roughly the size of one day's worth of data.

Default: `0`

##### --sst_files_soft_limit

Threshold for number of SST files per tablet. When exceeded, writes to a tablet will be throttled until the number of files is reduced.

Default: `24`

##### --sst_files_hard_limit

Threshold for number of SST files per tablet. When exceeded, writes to a tablet will no longer be allowed until the number of files is reduced.

Default: `48`

##### --file_expiration_ignore_value_ttl

When set to true, ignores any value-level TTL metadata when determining file expiration. Helpful in situations where some SST files are missing the necessary value-level metadata (in case of upgrade, for instance).

Default: `false`

{{< warning title="Warning">}}
Use of this flag can potentially result in expiration of live data. Use at your discretion.
{{< /warning >}}

##### --file_expiration_value_ttl_overrides_table_ttl

When set to true, allows files to expire purely based on their value-level TTL expiration time (even if it is lower than the table TTL). This is helpful for situations where a file needs to expire earlier than its table-level TTL would allow. If no value-level TTL metadata is available, then table-level TTL will still be used.

Default: `false`

{{< warning title="Warning">}}
Use of this flag can potentially result in expiration of live data. Use at your discretion.
{{< /warning >}}

## Concurrency control flags

To learn about Wait-on-Conflict concurrency control, see [Concurrency control](../../../architecture/transactions/concurrency-control/).

##### --enable_wait_queues

When set to true, enables in-memory wait queues, deadlock detection, and wait-on-conflict semantics in all YSQL traffic.

Default: `true`

##### --disable_deadlock_detection

When set to true, disables deadlock detection. If `enable_wait_queues=false`, this flag has no effect as deadlock detection is not running anyways.

Default: `false`

{{< warning title="Warning">}}
Use of this flag can potentially result in deadlocks that can't be resolved by YSQL. Use this flag only if the application layer can guarantee deadlock avoidance.
{{< /warning >}}

##### --wait_queue_poll_interval_ms

If `enable_wait_queues=true`, this controls the rate at which each tablet's wait queue polls transaction coordinators for the status of transactions which are blocking contentious resources.

Default: `100`

## Metric export flags

YB-TServer metrics are available in Prometheus format at `http://localhost:9000/prometheus-metrics`.

##### --export_help_and_type_in_prometheus_metrics

This flag controls whether #TYPE and #HELP information is included as part of the Prometheus metrics output by default.

To override this flag on a per-scrape basis, set the URL parameter `show_help` to `true` to include or to `false` to not include type and help information.  For example, querying `http://localhost:9000/prometheus-metrics?show_help=true` returns type and help information regardless of the setting of this flag.

Default: `true`

##### --max_prometheus_metric_entries

Introduced in version 2.21.1.0, this flag limits the number of Prometheus metric entries returned per scrape. If adding a metric with all its entities exceeds this limit, all entries from that metric are excluded. This could result in fewer entries than the set limit.

To override this flag on a per-scrape basis, you can adjust the URL parameter `max_metric_entries`.

Default: `UINT32_MAX`

## Catalog flags

Catalog flags are {{<badge/ea>}}.

##### ysql_catalog_preload_additional_table_list

Specifies the names of catalog tables (such as `pg_operator`, `pg_proc`, and `pg_amop`) to be preloaded by PostgreSQL backend processes. This flag reduces latency of first query execution of a particular statement on a connection.

Default: `""`

If [ysql_catalog_preload_additional_tables](#ysql-catalog-preload-additional-tables) is also specified, the union of the above specified catalog tables and `pg_am`, `pg_amproc`, `pg_cast`, and `pg_tablespace` is preloaded.

##### ysql_catalog_preload_additional_tables

When enabled, the postgres backend processes preload the `pg_am`, `pg_amproc`, `pg_cast`, and `pg_tablespace` catalog tables. This flag reduces latency of first query execution of a particular statement on a connection.

Default: `false`

If [ysql_catalog_preload_additional_table_list](#ysql-catalog-preload-additional-table-list) is also specified, the union of `pg_am`, `pg_amproc`, `pg_cast`, and `pg_tablespace` and the tables specified in `ysql_catalog_preload_additional_table_list` is preloaded.

##### ysql_enable_read_request_caching

Enables the YB-TServer catalog cache, which reduces YB-Master overhead for starting a connection and internal system catalog metadata refresh (for example, after executing a DDL), when there are many YSQL connections per node.

Default: `true`

##### ysql_minimal_catalog_caches_preload

Defines what part of the catalog gets cached and preloaded by default. As a rule of thumb, preloading more means lower first-query latency (as most/all necessary metadata will already be in the cache) at a cost of higher per-connection memory. Preloading less of the catalog means less memory though can result in a higher mean first-query latency (as we may need to ad-hoc lookup more catalog entries first time we execute a query). This flag only loads the system catalog tables (but not the user objects) which should keep memory low, while loading all often used objects. Still user-object will need to be loaded ad-hoc, which can make first-query latency a bit higher (most impactful in multi-region clusters).

Default: `false`

##### ysql_use_relcache_file

Controls whether to use the PostgreSQL relcache init file, which caches critical system catalog entries. If enabled, each PostgreSQL connection loads only this minimal set of cached entries (except if the relcache init file needs to be re-built, for example, after a DDL invalidates the cache). If disabled, each PostgreSQL connection preloads the catalog cache, which consumes more memory but reduces first query latency.

Default: `true`

##### ysql_yb_toast_catcache_threshold

Specifies the threshold (in bytes) beyond which catalog tuples will get compressed when they are stored in the PostgreSQL catalog cache. Setting this flag reduces memory usage for certain large objects, including functions and views, in exchange for slower catalog refreshes.

To minimize performance impact when enabling this flag, set it to 2KB or higher.

Default: -1 (disabled). Minimum: 128 bytes.

## DDL concurrency flags

DDL concurrency flags are {{<badge/tp>}}.

##### ysql_enable_db_catalog_version_mode

Enable the per database catalog version mode. A DDL statement that
affects the current database can only increment catalog version for
that database.

Default: `true`

{{< note title="Important" >}}

In earlier releases, after a DDL statement is executed, if the DDL statement increments the catalog version, then all the existing connections need to refresh catalog caches before
they execute the next statement. However, when per database catalog version mode is
enabled, multiple DDL statements can be concurrently executed if each DDL only
affects its current database and is executed in a separate database. Existing
connections only need to refresh their catalog caches if they are connected to
the same database as that of a DDL statement. It is recommended to keep the default value of this flag because per database catalog version mode helps to avoid unnecessary cross-database catalog cache refresh which is considered as an expensive operation.

{{< /note >}}

If you encounter any issues caused by per-database catalog version mode, you can disable per database catalog version mode using the following steps:

1. Shut down the cluster.

1. Start the cluster with `--ysql_enable_db_catalog_version_mode=false`.

1. Execute the following YSQL statements:

    ```sql
    SET yb_non_ddl_txn_for_sys_tables_allowed=true;
    SELECT yb_fix_catalog_version_table(false);
    SET yb_non_ddl_txn_for_sys_tables_allowed=false;
    ```

To re-enable the per database catalog version mode using the following steps:

1. Execute the following YSQL statements:

    ```sql
    SET yb_non_ddl_txn_for_sys_tables_allowed=true;
    SELECT yb_fix_catalog_version_table(true);
    SET yb_non_ddl_txn_for_sys_tables_allowed=false;
    ```

1. Shut down the cluster.
1. Start the cluster with `--ysql_enable_db_catalog_version_mode=true`.

##### enable_heartbeat_pg_catalog_versions_cache

Whether to enable the use of heartbeat catalog versions cache for the
`pg_yb_catalog_version` table which can help to reduce the number of reads
from the table. This is beneficial when there are many databases and/or
many yb-tservers in the cluster.

Note that `enable_heartbeat_pg_catalog_versions_cache` is only used when [ysql_enable_db_catalog_version_mode](#ysql-enable-db-catalog-version-mode) is true.

Default: `false`

{{< note title="Important" >}}

Each yb-tserver regularly sends a heartbeat request to the yb-master
leader. As part of the heartbeat response, yb-master leader reads all the rows
in the table `pg_yb_catalog_version` and sends the result back in the heartbeat
response. As there is one row in the table `pg_yb_catalog_version` for each
database, the cost of reading `table pg_yb_catalog_version` becomes more
expensive when the number of yb-tservers, or the number of databases goes up.

{{< /note >}}

## DDL atomicity flags

DDL atomicity flags are {{<badge/tp>}}.

##### ysql_yb_ddl_rollback_enabled

Enable DDL atomicity. When a DDL transaction that affects the DocDB system catalog fails, YB-Master will rollback the changes made to the DocDB system catalog.

Default: true

{{< note title="Important" >}}
In YSQL, a DDL statement creates a separate DDL transaction to execute the DDL statement. A DDL transaction generally needs to read and write PostgreSQL metadata stored in catalog tables in the same way as a native PostgreSQL DDL statement. In addition, some DDL statements also involve updating DocDB system catalog table.
(for example, a DDL statement such as `alter table add/drop column`). When a DDL transaction fails, the corresponding DDL statement is aborted. This means that the PostgreSQL metadata will be rolled back atomically.
<br>Before the introduction of the flag `--ysql_yb_ddl_rollback_enabled`, the DocDB system catalog changes were not automatically rolled back by YB-Master, possibly leading to metadata corruption that had to be manually fixed. Currently, with this flag being set to true, YB-Master can rollback the DocDB system catalog changes
automatically to prevent metadata corruption.
{{< /note >}}

##### report_ysql_ddl_txn_status_to_master

If set, at the end of a DDL operation, the YB-TServer notifies the YB-Master whether the DDL operation was committed or aborted.

Default: true

{{< note title="Important" >}}
Due to implementation restrictions, after a DDL statement commits or aborts, YB-Master performs a relatively expensive operation by continuously polling the transaction status tablet, and comparing the DocDB schema with PostgreSQL schema to determine whether the transaction was a success.<br> This behavior is optimized with the flag `report_ysql_ddl_txn_status_to_master`, where at the end of a DDL transaction, YSQL sends the status of the transaction (commit/abort) to YB-Master. Once received, YB-Master can stop polling the transaction status tablet, and also skip the relatively expensive schema comparison.
{{< /note >}}

##### ysql_ddl_transaction_wait_for_ddl_verification

If set, DDL transactions will wait for DDL verification to complete before returning to the client.

Default: true

{{< note title="Important" >}}
After a DDL statement that includes updating DocDB system catalog completes, YB-Master still needs to work on the DocDB system catalog changes in the background asynchronously, to ensure that they are eventually in sync with the corresponding PostgreSQL catalog changes. This can take additional time in order to reach eventual consistency. During this period, an immediately succeeding DML or DDL statement can fail due to changes made by YB-Master to the DocDB system catalog in the background, which may cause confusion.<br>
When the flag `ysql_ddl_transaction_wait_for_ddl_verification` is enabled, YSQL waits for any YB-Master background operations to finish before returning control to the user.
{{< /note >}}

## Advanced flags

##### backfill_index_client_rpc_timeout_ms

Timeout (in milliseconds) for the backfill stage of a concurrent CREATE INDEX.

Default: 86400000 (1 day)

##### backfill_index_timeout_grace_margin_ms

The time to exclude from the YB-Master flag [ysql_index_backfill_rpc_timeout_ms](../yb-master/#ysql-index-backfill-rpc-timeout-ms) in order to return results to YB-Master in the specified deadline. Should be set to at least the amount of time each batch would require, and less than `ysql_index_backfill_rpc_timeout_ms`.

Default: -1, where the system automatically calculates the value to be approximately 1 second.

##### backfill_index_write_batch_size

The number of table rows to backfill at a time. In case of [GIN indexes](../../../explore/ysql-language-features/indexes-constraints/gin/), the number can include more index rows.

Default: 128

## PostgreSQL server options

YugabyteDB uses PostgreSQL server configuration parameters to apply server configuration settings to new server instances.

### Modify configuration parameters

You can modify these parameters in the following ways:

- Use the [ysql_pg_conf_csv](#ysql-pg-conf-csv) flag.

- Set the option per-database:

    ```sql
    ALTER DATABASE database_name SET temp_file_limit=-1;
    ```

- Set the option per-role:

    ```sql
    ALTER ROLE yugabyte SET temp_file_limit=-1;
    ```

    When setting a parameter at the role or database level, you have to open a new session for the changes to take effect.

- Set the option for the current session:

    ```sql
    SET temp_file_limit=-1;
    --- alternative way
    SET SESSION temp_file_limit=-1;
    ```

    If `SET` is issued in a transaction that is aborted later, the effects of the SET command are reverted when the transaction is rolled back.

    If the surrounding transaction commits, the effects will persist for the whole session.

- Set the option for the current transaction:

    ```sql
    SET LOCAL temp_file_limit=-1;
    ```

- To specify the minimum age of a transaction (in seconds) before its locks are included in the results returned from querying the [pg_locks](../../../explore/observability/pg-locks/) view, use [yb_locks_min_txn_age](../../../explore/observability/pg-locks/#yb-locks-min-txn-age):

    ```sql
    --- To change the minimum transaction age to 5 seconds:
    SET session yb_locks_min_txn_age = 5000;
    ```

- To set the maximum number of transactions for which lock information is displayed when you query the [pg_locks](../../../explore/observability/pg-locks/) view, use [yb_locks_max_transactions](../../../explore/observability/pg-locks/#yb-locks-max-transactions):

    ```sql
    --- To change the maximum number of transactions to display to 10:
    SET session yb_locks_max_transactions = 10;
    ```

For information on available PostgreSQL server configuration parameters, refer to [Server Configuration](https://www.postgresql.org/docs/11/runtime-config.html) in the PostgreSQL documentation.

### YSQL configuration parameters

The server configuration parameters for YugabyteDB are the same as for PostgreSQL, with the following exceptions and additions.

##### log_line_prefix

YugabyteDB supports the following additional options for the `log_line_prefix` parameter:

- %C = cloud name
- %R = region / data center name
- %Z = availability zone / rack name
- %U = cluster UUID
- %N = node and cluster name
- %H = current hostname

For information on using `log_line_prefix`, refer to [log_line_prefix](https://www.postgresql.org/docs/11/runtime-config-logging.html#GUC-LOG-LINE-PREFIX) in the PostgreSQL documentation.

##### suppress_nonpg_logs (boolean)

When set, suppresses logging of non-PostgreSQL output to the PostgreSQL log file in the `tserver/logs` directory.

Default: `off`

##### temp_file_limit

Specifies the amount of disk space used for temporary files for each YSQL connection, such as sort and hash temporary files, or the storage file for a held cursor.

Any query whose disk space usage exceeds `temp_file_limit` will terminate with the error `ERROR:  temporary file size exceeds temp_file_limit`. Note that temporary tables do not count against this limit.

You can remove the limit (set the size to unlimited) using `temp_file_limit=-1`.

Valid values are `-1` (unlimited), `integer` (in kilobytes), `nMB` (in megabytes), and `nGB` (in gigabytes) (where 'n' is an integer).

Default: `1GB`

##### enable_bitmapscan

{{<badge/tp>}} Enables or disables the query planner's use of bitmap-scan plan types.

Bitmap Scans use multiple indexes to answer a query, with only one scan of the main table. Each index produces a "bitmap" indicating which rows of the main table are interesting. Multiple bitmaps can be combined with AND or OR operators to create a final bitmap that is used to collect rows from the main table.

Bitmap scans follow the same work_mem behavior as PostgreSQL: each individual bitmap is bounded by work_mem. If there are n bitmaps, it means we may use n * work_mem memory.

Bitmap scans are only supported for LSM indexes.

Default: false

##### yb_bnl_batch_size

Set the size of a tuple batch that's taken from the outer side of a [batched nested loop (BNL) join](../../../explore/ysql-language-features/join-strategies/#batched-nested-loop-join-bnl). When set to 1, BNLs are effectively turned off and won't be considered as a query plan candidate.

Default: 1024

##### yb_enable_batchednl

Enable or disable the query planner's use of batched nested loop join.

Default: true

##### yb_enable_base_scans_cost_model

{{<badge/tp>}} Enables the YugabyteDB cost model for Sequential and Index scans. When enabling this flag, it is also recommended to run ANALYZE on user tables to maintain up-to-date statistics.

Default: false

##### yb_enable_optimizer_statistics

{{<badge/tp>}} Enables use of the PostgreSQL selectivity estimation, which uses table statistics collected with ANALYZE.

Default: false

##### yb_fetch_size_limit

{{<badge/tp>}} Maximum size (in bytes) of total data returned in one response when the query layer fetches rows of a table from DocDB. Used to bound how many rows can be returned in one request. Set to 0 to have no size limit. To enable size based limit, `yb_fetch_row_limit` should be set to 0.

If both `yb_fetch_row_limit` and `yb_fetch_size_limit` are set then limit is taken as the lower bound of the two values.

See also the [--ysql_yb_fetch_size_limit](#ysql-yb-fetch-size-limit) flag. If the flag is set, this parameter takes precedence.

Default: 0

##### yb_fetch_row_limit

{{<badge/tp>}} Maximum number of rows returned in one response when the query layer fetches rows of a table from DocDB. Used to bound how many rows can be returned in one request. Set to 0 to have no row limit.

See also the [--ysql_yb_fetch_row_limit](#ysql-yb-fetch-row-limit) flag. If the flag is set, this parameter takes precedence.

Default: 1024

##### yb_use_hash_splitting_by_default

When set to true, tables and indexes are hash-partitioned based on the first column in the primary key or index. Setting this flag to false changes the first column in the primary key or index to be stored in ascending order.

Default: true

##### default_transaction_isolation

Specifies the default isolation level of each new transaction. Every transaction has an isolation level of `read uncommitted`, `read committed`, `repeatable read`, or `serializable`.

See [transaction isolation levels](../../../architecture/transactions/isolation-levels) for reference.

Default: `read committed`

## Admin UI

The Admin UI for the YB-TServer is available at `http://localhost:9000`.

### Dashboards

List of all dashboards to review ongoing operations.

![tserver-dashboards](/images/admin/tserver-dashboards.png)

### Tables

List of all tables managed by this specific instance, sorted by [namespace](../yb-master/#namespaces).

![tserver-tablets](/images/admin/tserver-tables.png)

### Tablets

List of all tablets managed by this specific instance.

![tserver-tablets](/images/admin/tserver-tablets.png)

### Debug

List of all utilities available to debug the performance of this specific instance.

![tserver-debug](/images/admin/tserver-debug.png)
