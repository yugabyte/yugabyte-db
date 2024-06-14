---
title: yb-master configuration reference
headerTitle: yb-master
linkTitle: yb-master
menu:
  preview:
    identifier: yb-master
    parent: configuration
    weight: 2450
type: docs
---

The [YB-Master]({{<version>}}/architecture/yb-master/) server acts a catalog service and an orchestration service in the YugabyteDB cluster. Here is a list of configuration flags that are supported.

{{<note>}}
The **yb-master** executable file is located in the **bin** directory of YugabyteDB home.
{{</note>}}


{{%collapse title="Syntax" %}}

```sh
yb-master [ flag  ] | [ flag ]
```

## Example

```sh
./bin/yb-master \
--master_addresses 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
--rpc_bind_addresses 172.151.17.130 \
--fs_data_dirs "/home/centos/disk1,/home/centos/disk2" \
--replication_factor=3
```

## Online help

To display the online help, run `yb-master --help` from the YugabyteDB home directory:

```sh
./bin/yb-master --help
```

{{%/collapse%}}

{{<nav/pages>}}
{{<nav/page title="General" href="../general-flags">}}
{{<nav/page title="Logging" href="../logging-flags">}}
{{<nav/page title="Memory" href="../memory-flags">}}
{{</nav/pages>}}

## Memory division flags

These flags are used to determine how the RAM of a node is split between the [master](../../../architecture/key-concepts/#master-server) process and other processes, including Postgres and a [TServer](../../../architecture/key-concepts/#tserver) process if present, as well as how to split memory inside of a master process between various internal components like the RocksDB block cache.

{{< warning title="Warning" >}}

Ensure you do not _oversubscribe memory_ when changing these flags: make sure the amount of memory reserved for the master process and TServer if present leaves enough memory on the node for Postgres, and any required other processes like monitoring agents plus the memory needed by the kernel.

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

To read this table, take your node's available memory in GiB, call it _M_, and find the column who's heading condition _M_ meets.  For example, a node with 7 GiB of available memory would fall under the column labeled "4 < _M_ &le; 8" because 4 < 7 &le; 8.  The defaults for [`--default_memory_limit_to_ram_ratio`](#default-memory-limit-to-ram-ratio) on this node will thus be `0.48` for TServers and `0.15` for masters. The Postgres and other percentages are not set via a flag currently but rather consist of whatever memory is left after TServer and master take their cut.  There is currently no distinction between Postgres and other memory except on [YugabyteDB Managed](/preview/yugabyte-cloud/) where a [cgroup](https://www.cybertec-postgresql.com/en/linux-cgroups-for-postgresql/) is used to limit the Postgres memory.

For comparison, when `--use_memory_defaults_optimized_for_ysql` is `false`, the split is TServer 85%, master 10%, Postgres 0%, and other 5%.

The defaults for the master process partitioning flags when `--use_memory_defaults_optimized_for_ysql` is `true` do not depend on the node size, and are described in the following table:

| flag | default |
| :--- | :--- |
| --db_block_cache_size_percentage | 25 |
| --tablet_overhead_size_percentage | 0 |

Currently these are the same as the defaults when `--use_memory_defaults_optimized_for_ysql` is `false`, but may change in future releases.

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

Default: `0.10` unless [`--use_memory_defaults_optimized_for_ysql`](#use-memory-defaults-optimized-for-ysql) is true.

### Flags controlling the split of memory within a master process

##### --db_block_cache_size_bytes

Size of the shared RocksDB block cache (in bytes).  A value of `-1` specifies to instead use a percentage of this processes' hard memory limit; see [`--db_block_cache_size_percentage`](#db-block-cache-size-percentage) for the percentage used.  A value of `-2` disables the block cache.

Default: `-1`

##### --db_block_cache_size_percentage

Percentage of the process' hard memory limit to use for the shared RocksDB block cache if [`--db_block_cache_size_bytes`](#db-block-cache-size-bytes) is `-1`.  The special value `-1000` means to instead use the default value for this flag.  The special value `-3` means to use an older default that does not take the amount of RAM into account.

Default: `25` unless [`--use_memory_defaults_optimized_for_ysql`](#use-memory-defaults-optimized-for-ysql) is true.

##### --tablet_overhead_size_percentage

Percentage of the process' hard memory limit to use for tablet-related overheads. A value of `0` means no limit.  Must be between `0` and `100` inclusive. Exception: `-1000` specifies to instead use the default value for this flag.

Each tablet replica generally requires 700 MiB of this memory.

Default: `0` unless [`--use_memory_defaults_optimized_for_ysql`](#use-memory-defaults-optimized-for-ysql) is true.

## Index

{{%table%}}
|Category|Flags|
|--------|-----|

|General|
[version](../general-flags#version),
[flagfile](../general-flags#flagfile),
[master_addresses](../general-flags#master-addresses),
[fs_data_dirs](../general-flags#fs-data-dirs),
[fs_wal_dirs](../general-flags#fs-wal-dirs),
[rpc_bind_addresses](../general-flags#rpc-bind-addresses),
[server_broadcast_addresses](../general-flags#server-broadcast-addresses),
[dns_cache_expiration_ms](../general-flags#dns-cache-expiration-ms),
[use_private_ip](../general-flags#use-private-ip),
[webserver_interface](../general-flags#webserver-interface),
[webserver_port](../general-flags#webserver-port),
[webserver_doc_root](../general-flags#webserver-doc-root),
[webserver_certificate_file](../general-flags#webserver-certificate-file),
[webserver_authentication_domain](../general-flags#webserver-authentication-domain),
[webserver_password_file](../general-flags#webserver-password-file),
[defer_index_backfill](../general-flags#defer-index-backfill),
[allow_batching_non_deferred_indexes](../general-flags#allow-batching-non-deferred-indexes),
[enable_ysql](../general-flags#enable-ysql),
|

|Logging|
[colorlogtostderr](../logging-flags#colorlogtostderr),
[logbuflevel](../logging-flags#logbuflevel),
[logbufsecs](../logging-flags#logbufsecs),
[logtostderr](../logging-flags#logtostderr),
[log_dir](../logging-flags#log-dir),
[log_link](../logging-flags#log-link),
[log_prefix](../logging-flags#log-prefix),
[max_log_size](../logging-flags#max-log-size),
[minloglevel](../logging-flags#minloglevel),
[stderrthreshold](../logging-flags#stderrthreshold),
[callhome_enabled](../logging-flags#callhome-enabled),
|

|Memory|
[use_memory_defaults_optimized_for_ysql](../memory-flags#use-memory-defaults-optimized-for-ysql),
[memory_limit_hard_bytes](../memory-flags#memory-limit-hard-bytes),
[default_memory_limit_to_ram_ratio](../memory-flags#default-memory-limit-to-ram-ratio),
[db_block_cache_size_bytes](../memory-flags#db-block-cache-size-bytes),
[db_block_cache_size_percentage](../memory-flags#db-block-cache-size-percentage),
[tablet_overhead_size_percentage](../memory-flags#tablet-overhead-size-percentage),
|
{{%/table%}}
