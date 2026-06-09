---
title: yb-master configuration reference
headerTitle: yb-master
linkTitle: yb-master
description: YugabyteDB Master Server (yb-master) binary and configuration flags to manage cluster metadata and coordinate cluster-wide operations.
menu:
  stable:
    identifier: yb-master
    parent: configuration
    weight: 2000
type: docs
body_class: configuration
rightNav:
  hideH4: true
---

The YugabyteDB Master Server ([YB-Master](../../../architecture/yb-master/)) coordinates cluster-wide control plane work: it stores and replicates system metadata, tracks tablet servers and tablets, drives cluster balancing and automatic tablet splitting, and participates in Raft consensus with other masters for highly available metadata. Together with YB-TServers, it helps keep the cluster consistent and operational as you scale or recover from failures.

This reference describes flags for configuring YB-Master processes in a YugabyteDB cluster. Use these flags to align behavior with your topology, security model, and operational goals.

Use the yb-master binary and its flags to configure the YB-Master server. The yb-master executable file is located in the `bin` directory of YugabyteDB home.

{{< note title="Setting flags in YugabyteDB Anywhere" >}}

If you are using YugabyteDB Anywhere, set flags using the [Edit Flags](../../../yugabyte-platform/manage-deployments/edit-config-flags/#modify-configuration-flags) feature.

{{< /note >}}

Flags are organized in the following categories.

| Category                     | Description |
|------------------------------|-------------|
| [General configuration](#general-configuration)        | Basic server setup including overall system settings, logging, web interface, and metrics export. |
| [PostgreSQL configuration parameters](#postgresql-configuration-parameters)        | Overview and pointers to PostgreSQL configuration; YSQL flags on YB-Master appear under [Security](#security). |
| [Networking](#networking)                   | Flags that control network interfaces, RPC endpoints, DNS caching, and geo-distribution settings. |
| [Storage and data management](#storage-data-management)    | Parameters for managing data directories, WAL configurations, cluster balancing, sharding, tablet splitting, CDC, and YSQL catalog behavior. |
| [Performance tuning](#performance-tuning)           | Options for resource allocation, memory management, Auto Analyze, advisory locks, Raft consensus, index backfill, and preview or advanced tuning flags. |
| [Security](#security)                     | Settings for encryption, SSL/TLS, and authentication to secure both node-to-node and client-server communications. |

**Legend**

Flags with specific characteristics are highlighted using the following badges:

- {{% tags/feature/restart-needed %}} – A restart of the server is required for the flag to take effect. For example, if the flag is used on *yb-master*, you need to restart only *yb-master*. If the flag is used on both the *yb-master* and *yb-tserver*, restart both services.
- {{% tags/feature/t-server %}} – The flag must have the same value across all *yb-master* and *yb-tserver* nodes.

**Syntax**

```sh
yb-master [ flag  ] | [ flag ]
```

**Example**

```sh
./bin/yb-master \
    --master_addresses 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
    --rpc_bind_addresses 172.151.17.130 \
    --fs_data_dirs "/home/centos/disk1,/home/centos/disk2" \
    --replication_factor=3
```

**Online help**

To display the online help, run `yb-master --help` from the YugabyteDB home directory:

```sh
./bin/yb-master --help
```

## All flags

The following sections describe the flags considered relevant to configuring YugabyteDB for production deployments. For a list of all flags, see [All YB-Master flags](../all-flags-yb-master/).

## General configuration

##### --flagfile

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
{{% /tags/wrap %}}

Specifies the configuration file to load flags from.

##### --version

Shows version and build information, then exits.

##### --master_addresses

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `127.0.0.1:7100`
{{% /tags/wrap %}}

Specifies a comma-separated list of all RPC addresses for YB-Master consensus-configuration.

{{< note title="Note" >}}

The number of comma-separated values should match the total number of YB-Master server (or the replication factor).

{{< /note >}}

Required.

##### --max_clock_skew_usec

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `500000` (500,000 µs = 500ms)
{{% /tags/wrap %}}

The expected maximum clock skew, in microseconds (µs), between any two servers in your deployment.

##### --time_source

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `""`
{{% /tags/wrap %}}

Specifies the time source used by the database. Set this to `clockbound` for configuring a highly accurate time source. Using `clockbound` requires [system configuration](../../../deploy/manual-deployment/system-config/#set-up-time-synchronization).

### Webserver configuration

##### --webserver_interface

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `""` (empty; the web UI binds to the first host IP from [--rpc_bind_addresses](#rpc-bind-addresses))
{{% /tags/wrap %}}

The address to bind for the web server user interface.

##### --webserver_port

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `7000`
{{% /tags/wrap %}}

The port for monitoring the web server.

##### --webserver_doc_root

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: The `www` directory in the YugabyteDB home directory.
{{% /tags/wrap %}}

The monitoring web server home directory.

##### --webserver_certificate_file

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `""`
{{% /tags/wrap %}}

Location of the SSL certificate file (in .pem format) to use for the web server. If empty, SSL is not enabled for the web server.

##### --webserver_authentication_domain

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `""`
{{% /tags/wrap %}}

Domain used for `.htpasswd` authentication. This should be used in conjunction with [--webserver_password_file](#webserver-password-file).

##### --webserver_password_file

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `""`
{{% /tags/wrap %}}

Location of the `.htpasswd` file containing usernames and hashed passwords, for authentication to the web server.

### Logging flags

##### --log_dir

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: Same as [--fs_data_dirs](#fs-data-dirs)
{{% /tags/wrap %}}

The directory to write YB-Master log files.

##### --colorlogtostderr

{{% tags/wrap %}}

Default: `false`
{{% /tags/wrap %}}

Color messages logged to `stderr` (if supported by terminal).

##### --logbuflevel

{{% tags/wrap %}}

Default: `1`
{{% /tags/wrap %}}

Buffer log messages logged at this level (or lower).

Valid values: `-1` (don't buffer); `0` (INFO); `1` (WARN); `2` (ERROR); `3` (FATAL)

##### --logbufsecs

{{% tags/wrap %}}

Default: `30`
{{% /tags/wrap %}}

Buffer log messages for at most this many seconds.

##### --logtostderr

{{% tags/wrap %}}

Default: `false`
{{% /tags/wrap %}}

Write log messages to `stderr` instead of `logfiles`.

##### --log_link

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `""`
{{% /tags/wrap %}}

Put additional links to the log files in this directory.

##### --log_prefix

{{% tags/wrap %}}

Default:  `true`
{{% /tags/wrap %}}

Prepend the log prefix to each log line.

##### --max_log_size

{{% tags/wrap %}}

Default: `1800` (1.8 GB)
{{% /tags/wrap %}}

The maximum log size, in megabytes (MB). A value of `0` will be silently overridden to `1`.

##### --minloglevel

{{% tags/wrap %}}

Default: `0` (INFO)
{{% /tags/wrap %}}

The minimum level to log messages. Values are: `0` (INFO), `1` (WARN), `2` (ERROR), `3` (FATAL).

##### --stderrthreshold

{{% tags/wrap %}}

Default: `3`
{{% /tags/wrap %}}

Log messages at, or above, this level are copied to `stderr` in addition to log files.

##### --callhome_enabled

{{% tags/wrap %}}

Default: `true`
{{% /tags/wrap %}}

Disable callhome diagnostics.

### Metric export flags

YB-Master metrics are available in Prometheus format at `http://localhost:7000/prometheus-metrics`.

##### --export_help_and_type_in_prometheus_metrics

{{% tags/wrap %}}

Default: `true`
{{% /tags/wrap %}}

This flag controls whether #TYPE and #HELP information is included as part of the Prometheus metrics output by default.

To override this flag on a per-scrape basis, set the URL parameter `show_help` to `true` to include, or to `false` to not include type and help information.  For example, querying `http://localhost:7000/prometheus-metrics?show_help=true` returns type and help information regardless of the setting of this flag.

##### --max_prometheus_metric_entries

{{% tags/wrap %}}

Default: `4294967295`
{{% /tags/wrap %}}

Introduced in version 2.21.1.0, this flag limits the number of Prometheus metric entries returned per scrape. If adding a metric with all its entities exceeds this limit, all entries from that metric are excluded. This could result in fewer entries than the set limit.

To override this flag on a per-scrape basis, you can adjust the URL parameter `max_metric_entries`.

## PostgreSQL configuration parameters

YugabyteDB uses [PostgreSQL server configuration parameters](https://www.postgresql.org/docs/15/config-setting.html) to apply server configuration settings.

YSQL-related flags for YB-Master are documented under [Security](#security) in the [YSQL](#ysql) subsection. For the complete PostgreSQL and YSQL flag reference on data nodes, see [PostgreSQL configuration parameters](../yb-tserver/#postgresql-configuration-parameters).

## Networking

### RPC and binding addresses

##### --tserver_master_addrs

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `127.0.0.1:7100`
{{% /tags/wrap %}}

Specifies a comma-separated list of all the YB-Master RPC addresses.

Required.

The number of comma-separated values should match the total number of YB-Master servers (or the replication factor).

##### --rpc_bind_addresses

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
{{% tags/feature/t-server %}}
{{% /tags/wrap %}}

Default: Private IP address of the host on which the server is running, as defined in `/home/yugabyte/master/conf/server.conf`. For example:

```sh
egrep -i rpc /home/yugabyte/master/conf/server.conf
--rpc_bind_addresses=172.161.x.x:7100
```

Specifies the comma-separated list of the network interface addresses to which to bind for RPC connections.

The values used must match on all yb-master and [yb-tserver](../yb-tserver/#rpc-bind-addresses) configurations.

Make sure that the [server_broadcast_addresses](#server-broadcast-addresses) flag is set correctly if the following applies:

- `rpc_bind_addresses` is set to `0.0.0.0`
- `rpc_bind_addresses` involves public IP addresses such as, for example, `0.0.0.0:7100`, which instructs the server to listen on all available network interfaces.

##### --server_broadcast_addresses

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `""`
{{% /tags/wrap %}}

Specifies the public IP or DNS hostname of the server (with an optional port). This value is used by servers to communicate with one another, depending on the connection policy parameter.

### Private IP and DNS caching

##### --dns_cache_expiration_ms

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
{{% tags/feature/t-server %}}
Default: `60000` (1 minute)
{{% /tags/wrap %}}

Specifies the duration, in milliseconds, until a cached DNS resolution expires. When hostnames are used instead of IP addresses, a DNS resolver must be queried to match hostnames to IP addresses. By using a local DNS cache to temporarily store DNS lookups, DNS queries can be resolved quicker and additional queries can be avoided. This reduces latency, improves load times, and reduces bandwidth and CPU consumption.

{{< note title="Note" >}}

If this value is changed from the default, make sure to add the same value to all YB-Master and YB-TSever configurations.

{{< /note >}}

##### --use_private_ip

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `never`
{{% /tags/wrap %}}

Specifies the policy that determines when to use private IP addresses for inter-node communication. Possible values are `never`, `zone`, `cloud`, and `region`. Based on the values of the [geo-distribution flags](#geo-distribution-flags).

Valid values for the policy are:

- `never` — Always use the [--server_broadcast_addresses](#server-broadcast-addresses).
- `zone` — Use the private IP if destination node is located in the same cloud, region and zone; use the [--server_broadcast_addresses](#server-broadcast-addresses) outside the zone.
- `cloud` - Use the private IP if destination node is located in the same cloud.
- `region` — Use the private IP address if destination node is located in the same cloud and region; use [--server_broadcast_addresses](#server-broadcast-addresses) outside the region.

### Geo-distribution flags

Settings related to managing geo-distributed clusters.

##### --placement_zone

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `rack1`
{{% /tags/wrap %}}

The name of the availability zone (AZ), or rack, where this instance is deployed.

{{<tip title="Rack awareness">}}
For on-premises deployments, consider racks as zones to treat them as fault domains.
{{</tip>}}

##### --placement_region

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `datacenter1`
{{% /tags/wrap %}}

Name of the region or data center where this instance is deployed.

##### --placement_cloud

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `cloud1`
{{% /tags/wrap %}}

Name of the cloud where this instance is deployed.

##### --placement_uuid

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `""`
{{% /tags/wrap %}}

The unique identifier for the cluster.

##### --force_global_transactions

{{% tags/wrap %}}

Default: `false`
{{% /tags/wrap %}}

If true, forces all transactions through this instance to always be global transactions that use the `system.transactions` transaction status table. This is equivalent to always setting the YSQL parameter `force_global_transaction = TRUE`.

{{< note title="Global transaction latency" >}}

Avoid setting this flag when possible. All distributed transactions _can_ run without issue as global transactions, but you may have significantly higher latency when committing transactions, because YugabyteDB must achieve consensus across multiple regions to write to `system.transactions`. When necessary, it is preferable to selectively set the YSQL parameter `force_global_transaction = TRUE` rather than setting this flag.

{{< /note >}}

##### --auto_create_local_transaction_tables

{{% tags/wrap %}}

Default: `true`
{{% /tags/wrap %}}

If true, transaction tables will be automatically created for any YSQL tablespace which has a placement and at least one other table in it.

### Network compression flags

Use the following two flags to configure RPC compression:

##### --enable_stream_compression

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `true`
{{% /tags/wrap %}}

Controls whether YugabyteDB uses RPC compression.

##### --stream_compression_algo

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `0`
{{% /tags/wrap %}}

Specifies which RPC compression algorithm to use. Requires `enable_stream_compression` to be set to true. Valid values are:

0: No compression (default value)

1: Gzip

2: Snappy

3: LZ4

In most cases, LZ4 (`--stream_compression_algo=3`) offers the best compromise of compression performance versus CPU overhead. However, the default is set to 0, to avoid latency penalty on workloads.

## Storage & data management

### Filesystem and WAL directories

##### --fs_data_dirs

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
{{% /tags/wrap %}}

Specifies a comma-separated list of mount directories, where yb-master will add a `yb-data/master` data directory, `master.err`, `master.out`, and `pg_data` directory.

Required.

Changing the value of this flag after the cluster has already been created is not supported.

##### --fs_wal_dirs

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: Same value as `--fs_data_dirs`
{{% /tags/wrap %}}

Specifies a comma-separated list of directories, where yb-master will store write-ahead (WAL) logs. This can be the same as one of the directories listed in `--fs_data_dirs`, but not a subdirectory of a data directory.

### Write ahead log (WAL) flags

In a typical deployment, the values used for write ahead log (WAL) flags in [yb-tserver](../yb-tserver/#write-ahead-log-wal-flags) configurations should align with those in yb-master configurations.

##### --durable_wal_write

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `true`
{{% /tags/wrap %}}

If set to `false`, the writes to the WAL are synced to disk every [interval_durable_wal_write_ms](#interval-durable-wal-write-ms) milliseconds (ms) or every [bytes_durable_wal_write_mb](#bytes-durable-wal-write-mb) megabyte (MB), whichever comes first. Using `false` is recommended only for multi-AZ or multi-region deployments where the availability zones (AZs) or regions are independent failure domains and there is not a risk of correlated power loss. For single-AZ deployments, keep this flag `true`.

##### --interval_durable_wal_write_ms

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `1000`
{{% /tags/wrap %}}

When [--durable_wal_write](#durable-wal-write) is false, writes to the WAL are synced to disk every `--interval_durable_wal_write_ms` or [--bytes_durable_wal_write_mb](#bytes-durable-wal-write-mb), whichever comes first.

##### --bytes_durable_wal_write_mb

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `1`
{{% /tags/wrap %}}

When [--durable_wal_write](#durable-wal-write) is `false`, writes to the WAL are synced to disk every `--bytes_durable_wal_write_mb` or `--interval_durable_wal_write_ms`, whichever comes first.

##### --log_min_seconds_to_retain

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
Default: `7200` (2 hours)
{{% /tags/wrap %}}

The minimum duration, in seconds, to retain WAL segments, regardless of durability requirements. WAL segments can be retained for a longer amount of time, if they are necessary for correct restart. This value should be set long enough such that a tablet server which has temporarily failed can be restarted in the given time period.

The `--log_min_seconds_to_retain` value should match the value for [--follower_unavailable_considered_failed_sec](#follower-unavailable-considered-failed-sec).

##### --log_min_segments_to_retain

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
Default: `2`
{{% /tags/wrap %}}

The minimum number of WAL segments (files) to retain, regardless of durability requirements. The value must be at least `1`.

##### --log_segment_size_mb

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
{{<tags/feature/restart-needed>}}
Default: `64`
{{% /tags/wrap %}}

The size, in megabytes (MB), of a WAL segment (file). When the WAL segment reaches the specified size, then a log rollover occurs and a new WAL segment file is created.

##### --reuse_unclosed_segment_threshold_bytes

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
Default: `9223372036854775807`
{{% /tags/wrap %}}

When the server restarts from a previous crash, if the tablet's last WAL file size is less than or equal to this threshold value, the last WAL file will be reused. Otherwise, WAL will allocate a new file at bootstrap. To disable WAL reuse, set the value to `-1`.

### Cluster balancing flags

For information on YB-Master cluster balancing, see [Cluster balancing](../../../architecture/yb-master/#cluster-balancing).

For cluster balancing commands in yb-admin, see [Cluster balancing commands (yb-admin)](../../../admin/yb-admin/#cluster-balancing-commands).

For detailed information on cluster balancing scenarios, monitoring, and configuration, see [Cluster balancing](../../../architecture/docdb-sharding/cluster-balancing/).

##### --enable_load_balancing

{{% tags/wrap %}}

Default: `true`
{{% /tags/wrap %}}

Enables or disables the cluster balancing algorithm, to move tablets around.

##### --leader_balance_threshold

{{% tags/wrap %}}

Default: `0`
{{% /tags/wrap %}}

Specifies the number of leaders per tablet server to balance below. If this is configured to `0` (the default), the leaders will be balanced optimally at extra cost.

##### --leader_balance_unresponsive_timeout_ms

{{% tags/wrap %}}

Default: `3000` (3 seconds)
{{% /tags/wrap %}}

Specifies the period of time, in milliseconds, that a YB-Master can go without receiving a heartbeat from a YB-TServer before considering it unresponsive. Unresponsive servers are excluded from leader balancing.

##### --load_balancer_max_concurrent_adds

{{% tags/wrap %}}

Default: `25`
{{% /tags/wrap %}}

Specifies the maximum number of tablet peer replicas to add in a cluster balancer operations.

##### --load_balancer_max_concurrent_moves

{{% tags/wrap %}}

Default: `100`
{{% /tags/wrap %}}

Specifies the maximum number of tablet leaders on tablet servers (across the cluster) to move in any one run of the cluster balancer.

##### --load_balancer_max_concurrent_moves_per_table

{{% tags/wrap %}}

Default: `-1`
{{% /tags/wrap %}}

Specifies the maximum number of tablet leaders per table to move in any one run of the cluster balancer. The maximum number of tablet leader moves across the cluster is still limited by the flag `load_balancer_max_concurrent_moves`. This flag is meant to prevent a single table from using all of the leader moves quota and starving other tables. If set to -1, the number of leader moves per table is set to the global number of leader moves (`load_balancer_max_concurrent_moves`).

##### --load_balancer_max_concurrent_removals

{{% tags/wrap %}}

Default: `50`
{{% /tags/wrap %}}

Specifies the maximum number of over-replicated tablet peer removals to do in any one run of the cluster balancer. A value less than 0 means no limit.

##### --load_balancer_max_concurrent_tablet_remote_bootstraps

{{% tags/wrap %}}

Default: `-1`
{{% /tags/wrap %}}

Specifies the maximum number of tablets being remote bootstrapped across the cluster.

##### --load_balancer_max_concurrent_tablet_remote_bootstraps_per_table

{{% tags/wrap %}}

Default: `-1`
{{% /tags/wrap %}}

Maximum number of tablets being remote bootstrapped for any table. The maximum number of remote bootstraps across the cluster is still limited by the flag `load_balancer_max_concurrent_tablet_remote_bootstraps`. This flag is meant to prevent a single table use all the available remote bootstrap sessions and starving other tables.

##### --load_balancer_max_over_replicated_tablets

{{% tags/wrap %}}

Default: `50`
{{% /tags/wrap %}}

Specifies the maximum number of running tablet replicas per table that are allowed to be over the configured replication factor. This controls the amount of space amplification in the cluster when tablet removal is slow. A value less than 0 means no limit.

##### --load_balancer_num_idle_runs

{{% tags/wrap %}}

Default: `5`
{{% /tags/wrap %}}

Specifies the number of idle runs of load balancer to deem it idle.

##### --load_balancer_skip_leader_as_remove_victim

{{% tags/wrap %}}
{{<tags/feature/deprecated>}}
Default: `false`
{{% /tags/wrap %}}

Should the LB skip a leader as a possible remove candidate.

### Sharding flags

##### --replication_factor

{{% tags/wrap %}}

Default: `3`
{{% /tags/wrap %}}

The number of replicas, or copies of data, to store for each tablet in the universe.

##### --yb_num_shards_per_tserver

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
Default: `-1`, where the number of shards is determined at runtime, as follows:
{{% /tags/wrap %}}

The number of shards (tablets) per YB-TServer for each YCQL table when a user table is created.

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
- The [CREATE TABLE ... WITH TABLETS = <num>](../../../api/ycql/ddl_create_table/#create-a-table-specifying-the-number-of-tablets) clause can be used on a per-table basis to override the `yb_num_shards_per_tserver` value.

{{< /note >}}

##### --ysql_num_shards_per_tserver

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
Default: `-1`, where the number of shards is determined at runtime, as follows:
{{% /tags/wrap %}}

The number of shards (tablets) per YB-TServer for each YSQL table when a user table is created.

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
- The [CREATE TABLE ...SPLIT INTO](../../../api/ysql/the-sql-language/statements/ddl_create_table/#split-into) clause can be used on a per-table basis to override the `ysql_num_shards_per_tserver` value.

{{< /note >}}

##### --cleanup_split_tablets_interval_sec

{{% tags/wrap %}}

Default: `60`
{{% /tags/wrap %}}

Interval at which the tablet manager tries to cleanup split tablets that are no longer needed. Setting this to 0 disables cleanup of split tablets.

##### --ysql_colocate_database_by_default

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

When enabled, all databases created in the cluster are colocated by default. If you enable the flag after creating a cluster, you need to restart the YB-Master and YB-TServer services.

For more details, see [clusters in colocated tables](../../../additional-features/colocation/).

##### enforce_tablet_replica_limits

{{% tags/wrap %}}

Default: `false`
{{% /tags/wrap %}}

Enables/disables blocking of requests which would bring the total number of tablets in the system over a limit. For more information, see [Tablet limits](../../../architecture/docdb-sharding/tablet-splitting/#tablet-limits).

##### split_respects_tablet_replica_limits

{{% tags/wrap %}}

Default: `false`
{{% /tags/wrap %}}

If set, tablets will not be split if the total number of tablet replicas in the cluster after the split would exceed the limit after the split.

##### tablet_replicas_per_core_limit

{{% tags/wrap %}}

Default: `0` for no limit.
{{% /tags/wrap %}}

The number of tablet replicas that each core on a YB-TServer can support.

##### tablet_replicas_per_gib_limit

{{% tags/wrap %}}

Default: `1462`
{{% /tags/wrap %}}

The number of tablet replicas that each GiB reserved by YB-TServers for tablet overheads can support.

### Tablet splitting flags

##### --max_create_tablets_per_ts

{{% tags/wrap %}}

Default: `50`
{{% /tags/wrap %}}

The maximum number of tablets per tablet server that can be specified when creating a table. This also limits the number of tablets that can be created by tablet splitting.

##### --enable_automatic_tablet_splitting

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
Default: Auto flag — initial `false`, target `true` (after promotion, behaves as `true`; see [All YB-Master flags](../all-flags-yb-master/) for full metadata)
{{% /tags/wrap %}}

Enables YugabyteDB to [automatically split tablets](../../../architecture/docdb-sharding/tablet-splitting/#automatic-tablet-splitting), based on the specified tablet threshold sizes configured below.

{{< note title="Important" >}}

This value must match on all yb-master and yb-tserver configurations of a YugabyteDB cluster.

{{< /note >}}

##### --tablet_split_low_phase_shard_count_per_node

{{% tags/wrap %}}

Default: `1`
{{% /tags/wrap %}}

The threshold number of shards (per cluster node) in a table below which automatic tablet splitting will use [--tablet_split_low_phase_size_threshold_bytes](./#tablet-split-low-phase-size-threshold-bytes) to determine which tablets to split.

##### --tablet_split_low_phase_size_threshold_bytes

{{% tags/wrap %}}

Default: `134217728`
{{% /tags/wrap %}}

The size threshold used to determine if a tablet should be split when the tablet's table is in the "low" phase of automatic tablet splitting. See [--tablet_split_low_phase_shard_count_per_node](./#tablet-split-low-phase-shard-count-per-node).

##### --tablet_split_high_phase_shard_count_per_node

{{% tags/wrap %}}

Default: `24`
{{% /tags/wrap %}}

The threshold number of shards (per cluster node) in a table below which automatic tablet splitting will use [--tablet_split_high_phase_size_threshold_bytes](./#tablet-split-high-phase-size-threshold-bytes) to determine which tablets to split.

##### --tablet_split_high_phase_size_threshold_bytes

{{% tags/wrap %}}

Default: `10737418240`
{{% /tags/wrap %}}

The size threshold used to determine if a tablet should be split when the tablet's table is in the "high" phase of automatic tablet splitting. See [--tablet_split_high_phase_shard_count_per_node](./#tablet-split-high-phase-shard-count-per-node).

##### --tablet_force_split_threshold_bytes

{{% tags/wrap %}}

Default: `107374182400`
{{% /tags/wrap %}}

The size threshold used to determine if a tablet should be split even if the table's number of shards puts it past the "high phase".

##### --tablet_split_limit_per_table

{{% tags/wrap %}}

Default: `0`
{{% /tags/wrap %}}

The maximum number of tablets per table for tablet splitting. Limitation is disabled if this value is set to 0.

##### --index_backfill_tablet_split_completion_timeout_sec

{{% tags/wrap %}}

Default: `30`
{{% /tags/wrap %}}

Total time to wait for tablet splitting to complete on a table on which a backfill is running before aborting the backfill and marking it as failed.

##### --index_backfill_tablet_split_completion_poll_freq_ms

{{% tags/wrap %}}

Default: `2000`
{{% /tags/wrap %}}

Delay before retrying to see if tablet splitting has completed on the table on which a backfill is running.

##### --process_split_tablet_candidates_interval_msec

{{% tags/wrap %}}

Default: `0`
{{% /tags/wrap %}}

The minimum time between automatic splitting attempts. The actual splitting time between runs is also affected by `catalog_manager_bg_task_wait_ms`, which controls how long the background tasks thread sleeps at the end of each loop.

##### --outstanding_tablet_split_limit

{{% tags/wrap %}}

Default: `0`
{{% /tags/wrap %}}

Limits the number of total outstanding tablet splits. Limitation is disabled if value is set to `0`. Limit includes tablets that are performing post-split compactions.

##### --outstanding_tablet_split_limit_per_tserver

{{% tags/wrap %}}

Default: `1`
{{% /tags/wrap %}}

Limits the number of outstanding tablet splits per node. Limitation is disabled if value is set to `0`. Limit includes tablets that are performing post-split compactions.

##### --enable_tablet_split_of_pitr_tables

{{% tags/wrap %}}

Default: `true`
{{% /tags/wrap %}}

Enables automatic tablet splitting of tables covered by Point-In-Time Recovery schedules.

##### --prevent_split_for_ttl_tables_for_seconds

{{% tags/wrap %}}

Default: `86400`
{{% /tags/wrap %}}

Number of seconds between checks for whether to split a tablet with a default TTL. Checks are disabled if this value is set to 0.

##### --prevent_split_for_small_key_range_tablets_for_seconds

{{% tags/wrap %}}

Default: `300`
{{% /tags/wrap %}}

Number of seconds between checks for whether to split a tablet whose key range is too small to be split. Checks are disabled if this value is set to 0.

##### --sort_automatic_tablet_splitting_candidates

{{% tags/wrap %}}

Default: `true`
{{% /tags/wrap %}}

Determines whether to sort automatic split candidates from largest to smallest (prioritizing larger tablets for split).

Syntax:

```sh
yb-admin --master_addresses <master-addresses> --tablet_force_split_size_threshold_bytes <bytes>
```

- *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
- *bytes*: The threshold size, in bytes, after which tablets should be split. Default value of `0` disables automatic tablet splitting.

For details on automatic tablet splitting, see the following:

- [Automatic tablet splitting](../../../architecture/docdb-sharding/tablet-splitting/) — Architecture overview.
- [Automatic Re-sharding of Data with Tablet Splitting](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/docdb-automatic-tablet-splitting.md) — Architecture design document in the GitHub repository.

### DDL atomicity flags

##### ysql_yb_ddl_rollback_enabled

{{% tags/wrap %}}

Default: `true`
{{% /tags/wrap %}}

Enable DDL atomicity. When a DDL transaction that affects the DocDB system catalog fails, YB-Master will roll back the changes made to the DocDB system catalog.

{{< note title="Important" >}}
In YSQL, a DDL statement creates a separate DDL transaction to execute the DDL statement. A DDL transaction generally needs to read and write PostgreSQL metadata stored in catalog tables in the same way as a native PostgreSQL DDL statement. In addition, some DDL statements also involve updating DocDB system catalog table (for example, a DDL statement such as `alter table add/drop column`). When a DDL transaction fails, the corresponding DDL statement is aborted. This means that the PostgreSQL metadata will be rolled back atomically.

Before the introduction of the flag `--ysql_yb_ddl_rollback_enabled`, the DocDB system catalog changes were not automatically rolled back by YB-Master, possibly leading to metadata corruption that had to be manually fixed. Currently, with this flag being set to true, YB-Master can rollback the DocDB system catalog changes automatically to prevent metadata corruption.
{{< /note >}}

##### report_ysql_ddl_txn_status_to_master

{{% tags/wrap %}}

Default: `true`
{{% /tags/wrap %}}

If set, at the end of a DDL operation the YB-TServer notifies the YB-Master whether the DDL operation was committed or aborted.

{{< note title="Important" >}}
Due to implementation restrictions, after a DDL statement commits or aborts, YB-Master performs a relatively expensive operation by continuously polling the transaction status tablet, and comparing the DocDB schema with PostgreSQL schema to determine whether the transaction was a success.

This behavior is optimized with the flag `report_ysql_ddl_txn_status_to_master`, where at the end of a DDL transaction, YSQL sends the status of the transaction (commit/abort) to YB-Master. Once received, YB-Master can stop polling the transaction status tablet, and also skip the relatively expensive schema comparison.
{{< /note >}}

##### ysql_ddl_transaction_wait_for_ddl_verification

{{% tags/wrap %}}

Default: `true`
{{% /tags/wrap %}}

If set, DDL transactions will wait for DDL verification to complete before returning to the client.

{{< note title="Important" >}}
After a DDL statement that includes updating DocDB system catalog completes, YB-Master still needs to work on the DocDB system catalog changes in the background asynchronously, to ensure that they are eventually in sync with the corresponding PostgreSQL catalog changes. This can take additional time in order to reach eventual consistency. During this period, an immediately succeeding DML or DDL statement can fail due to changes made by YB-Master to the DocDB system catalog in the background, which may cause confusion.

When the flag `ysql_ddl_transaction_wait_for_ddl_verification` is enabled, YSQL waits for any YB-Master background operations to finish before returning control to the user.
{{< /note >}}

### Change data capture (CDC) flags

To learn about CDC, see [Change data capture (CDC)](../../../architecture/docdb-replication/change-data-capture/).

For information on other CDC configuration flags, see [YB-TServer's CDC flags](../yb-tserver/#change-data-capture-cdc-flags).

##### --yb_enable_cdc_consistent_snapshot_streams

{{% tags/wrap %}}
{{<tags/feature/tp>}}
Default: `false`
{{% /tags/wrap %}}

Support for creating a stream for Transactional CDC is currently in [Tech Preview](/stable/releases/versioning/#feature-maturity).

Enable support for creating streams for transactional CDC.

##### --cdc_state_checkpoint_update_interval_ms

{{% tags/wrap %}}

Default: `15000`
{{% /tags/wrap %}}

The rate at which CDC state's checkpoint is updated.

##### --cdc_snapshot_batch_size

{{% tags/wrap %}}

Default: `250`
{{% /tags/wrap %}}

Number of records fetched in a single batch of snapshot operation of CDC.

##### --cdc_min_replicated_index_considered_stale_secs

{{% tags/wrap %}}

Default: `900` (15 minutes)
{{% /tags/wrap %}}

If `cdc_min_replicated_index` hasn't been replicated in this amount of time, we reset its value to max int64 to avoid retaining any logs.

##### --timestamp_history_retention_interval_sec

{{% tags/wrap %}}

Default: `900` (15 minutes)
{{% /tags/wrap %}}

Time interval (in seconds) to retain history or older versions of data.

##### --update_min_cdc_indices_interval_secs

{{% tags/wrap %}}

Default: `60`
{{% /tags/wrap %}}

How often to read the `cdc_state` table to get the minimum applied index for each tablet across all streams. This information is used to correctly keep log files that contain unapplied entries. This is also the rate at which a tablet's minimum replicated index across all streams is sent to the other peers in the configuration. If flag `enable_log_retention_by_op_idx` (default: `true`) is disabled, this flag has no effect.

##### --cdc_checkpoint_opid_interval_ms

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `60000`
{{% /tags/wrap %}}

{{< warning title="Warning" >}}

If you are using multiple streams, it is advised that you set this flag to `1800000` (30 minutes).

{{< /warning >}}

##### --log_max_seconds_to_retain

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `86400`
{{% /tags/wrap %}}

Number of seconds to retain log files. Log files older than this value will be deleted even if they contain unreplicated CDC entries. If 0, this flag will be ignored. This flag is ignored if a log segment contains entries that haven't been flushed to RocksDB.

##### --log_stop_retaining_min_disk_mb

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `102400`
{{% /tags/wrap %}}

Stop retaining logs if the space available for the logs falls below this limit, specified in megabytes. As with `log_max_seconds_to_retain`, this flag is ignored if a log segment contains unflushed entries.

##### --cdc_max_stream_intent_records

{{% tags/wrap %}}

Default: `1680`
{{% /tags/wrap %}}

Maximum number of intent records allowed in a single CDC batch.

##### --cdc_state_table_num_tablets

{{% tags/wrap %}}

Default: `0` (Use the same default number of tablets as for regular tables.)
{{% /tags/wrap %}}

The number of tablets to use when creating the CDC state table. Used in both xCluster and CDCSDK.

##### --cdc_wal_retention_time_secs

{{% tags/wrap %}}

Default: `28800` (8 hours)
{{% /tags/wrap %}}

WAL retention time, in seconds, to be used for tables for which a CDC stream was created. Used in both xCluster and CDCSDK.

##### --cdc_intent_retention_ms

{{% tags/wrap %}}

Default: `28800000` (8 hours)
{{% /tags/wrap %}}

The time period, in milliseconds, after which the intents will be cleaned up if there is no client polling for the change records.

##### --cdcsdk_tablet_not_of_interest_timeout_secs

{{% tags/wrap %}}

Default: `14400` (4 hours)
{{% /tags/wrap %}}

Timeout after which it is inferred that a particular tablet is not of interest for CDC. To indicate that a particular tablet is of interest for CDC, it should be polled at least once within this interval of stream / slot creation.

##### --enable_tablet_split_of_cdcsdk_streamed_tables

{{% tags/wrap %}}

Default: `true`
{{% /tags/wrap %}}

Toggle automatic tablet splitting for tables in a CDCSDK stream, enhancing user control over replication processes.

##### --enable_truncate_cdcsdk_table

{{% tags/wrap %}}

Default: `false`
{{% /tags/wrap %}}

By default, TRUNCATE commands on tables with an active CDCSDK stream will fail. Change this flag to `true` to enable truncating tables.

##### --enable_tablet_split_of_replication_slot_streamed_tables

{{% tags/wrap %}}

Default: `true`
{{% /tags/wrap %}}

Toggle automatic tablet splitting for tables under replication slot. Applicable only to CDC using the [PostgreSQL logical replication protocol](../../../additional-features/change-data-capture/using-logical-replication/).

### LISTEN/NOTIFY flags

{{<tags/feature/ea idea="1901">}}Available in v2025.2.3 and later. To learn about LISTEN/NOTIFY, see [LISTEN, NOTIFY, and UNLISTEN](../../../api/ysql/the-sql-language/statements/cmd_listen_notify/).

##### --ysql_yb_enable_listen_notify

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
Default: `false`
{{% /tags/wrap %}}

Enables YSQL LISTEN/NOTIFY.

### File expiration based on TTL flags

##### --tablet_enable_ttl_file_filter

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

Turn on the file expiration for TTL feature.

##### --rocksdb_max_file_size_for_compaction

{{% tags/wrap %}}

Default: `0`
{{% /tags/wrap %}}

For tables with a `default_time_to_live` table property, sets a size threshold at which files will no longer be considered for compaction. Files over this threshold will still be considered for expiration. Disabled if value is `0`.

Ideally, `rocksdb_max_file_size_for_compaction` should strike a balance between expiring data at a reasonable frequency and not creating too many SST files (which can impact read performance). For instance, if 90 days worth of data is stored, consider setting this flag to roughly the size of one day's worth of data.

If `rocksdb_max_file_size_for_compaction` was set to a certain value on a cluster and then it needs to be increased, then it is highly likely that all the existing files on the tablets will become eligible for background compactions and a lot of compaction activity will occur in the system. This can lead to system instability if the concurrent user activity in the system is high. If TTL flags need to be tuned in order to accomodate increased usage on the table, it is better to consider splitting the tablet or increasing the `sst_files_soft_limit` and `sst_files_hard_limit` instead.

##### --sst_files_soft_limit

{{% tags/wrap %}}

Default: `24`
{{% /tags/wrap %}}

Threshold for number of SST files per tablet. When exceeded, writes to a tablet will be throttled until the number of files is reduced.

##### --sst_files_hard_limit

{{% tags/wrap %}}

Default: `48`
{{% /tags/wrap %}}

Threshold for number of SST files per tablet. When exceeded, writes to a tablet will no longer be allowed until the number of files is reduced.

##### --file_expiration_ignore_value_ttl

{{% tags/wrap %}}

Default: `false`
{{% /tags/wrap %}}

When set to true, ignores any value-level TTL metadata when determining file expiration. Helpful in situations where some SST files are missing the necessary value-level metadata (in case of upgrade, for instance).

{{< warning title="Warning">}}
Use of this flag can potentially result in expiration of live data. Use at your discretion.
{{< /warning >}}

##### --file_expiration_value_ttl_overrides_table_ttl

{{% tags/wrap %}}

Default: `false`
{{% /tags/wrap %}}

When set to true, allows files to expire purely based on their value-level TTL expiration time (even if it is lower than the table TTL). This is helpful for situations where a file needs to expire earlier than its table-level TTL would allow. If no value-level TTL metadata is available, then table-level TTL will still be used.

{{< warning title="Warning">}}
Use of this flag can potentially result in expiration of live data. Use at your discretion.
{{< /warning >}}

### Packed row flags

The packed row format for the YSQL API is {{<tags/feature/ga>}} as of v2.20.0, and for the YCQL API is {{<tags/feature/tp>}}.

To learn about the packed row feature, see [Packed rows in DocDB](../../../architecture/docdb/packed-rows/) in the architecture section.

##### --ysql_enable_packed_row

{{% tags/wrap %}}

Default: `true`
{{% /tags/wrap %}}

Whether packed row is enabled for YSQL.

Packed Row for YSQL can be used from version 2.16.4 in production environments if the cluster is not used in xCluster settings. For xCluster scenarios, use version 2.18.1 and later. Starting from version 2.19 and later, the flag default is true for new clusters.

##### --ysql_enable_packed_row_for_colocated_table

{{% tags/wrap %}}

Default: `true`
{{% /tags/wrap %}}

Whether packed row is enabled for colocated tables in YSQL. The colocated table has an additional flag to mitigate [#15143](https://github.com/yugabyte/yugabyte-db/issues/15143).

##### --ysql_packed_row_size_limit

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `0`
{{% /tags/wrap %}}

Packed row size limit for YSQL. The default value is 0 (use block size as limit). For rows that are over this size limit, a greedy approach will be used to pack as many columns as possible, with the remaining columns stored as individual key-value pairs.

##### --ycql_enable_packed_row

{{% tags/wrap %}}
{{<tags/feature/tp>}}
Default: `false`
{{% /tags/wrap %}}

YCQL packed row support is currently in [Tech Preview](/stable/releases/versioning/#feature-maturity).

Whether packed row is enabled for YCQL.

##### --ycql_packed_row_size_limit

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `0`
{{% /tags/wrap %}}

Packed row size limit for YCQL. The default value is 0 (use block size as limit). For rows that are over this size limit, a greedy approach will be used to pack as many columns as possible, with the remaining columns stored as individual key-value pairs.

### Catalog flags

For information on setting these flags, see [Customize preloading of YSQL catalog caches](../../../best-practices-operations/ysql-catalog-cache-tuning-guide/).

##### --ysql_catalog_preload_additional_table_list

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `""`
{{% /tags/wrap %}}

Specifies the names of catalog tables (such as `pg_operator`, `pg_proc`, and `pg_amop`) to be preloaded by PostgreSQL backend processes. This flag reduces latency of first query execution of a particular statement on a connection.

If [ysql_catalog_preload_additional_tables](#ysql-catalog-preload-additional-tables) is also specified, the union of the above specified catalog tables and `pg_am`, `pg_amproc`, `pg_cast`, and `pg_tablespace` is preloaded.

##### --ysql_catalog_preload_additional_tables

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

When enabled, the PostgreSQL backend processes preload the `pg_am`, `pg_amproc`, `pg_cast`, and `pg_tablespace` catalog tables. This flag reduces latency of first query execution of a particular statement on a connection.

If [ysql_catalog_preload_additional_table_list](#ysql-catalog-preload-additional-table-list) is also specified, the union of `pg_am`, `pg_amproc`, `pg_cast`, and `pg_tablespace` and the tables specified in `ysql_catalog_preload_additional_table_list` is preloaded.

##### --ysql_enable_read_request_caching

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `true`
{{% /tags/wrap %}}

Enables the YB-TServer catalog cache, which reduces YB-Master overhead for starting a connection and internal system catalog metadata refresh (for example, after executing a DDL), when there are many YSQL connections per node.

##### --ysql_minimal_catalog_caches_preload

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

Defines what part of the catalog gets cached and preloaded by default. As a rule of thumb, preloading more means lower first-query latency (as most/all necessary metadata will already be in the cache) at a cost of higher per-connection memory. Preloading less of the catalog means less memory though can result in a higher mean first-query latency (as we may need to ad-hoc lookup more catalog entries first time we execute a query). This flag only loads the system catalog tables (but not the user objects) which should keep memory low, while loading all often used objects. Still user-object will need to be loaded ad-hoc, which can make first-query latency a bit higher (most impactful in multi-region clusters).

##### --ysql_use_relcache_file

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `true`
{{% /tags/wrap %}}

Controls whether to use the PostgreSQL relcache init file, which caches critical system catalog entries. If enabled, each PostgreSQL connection loads only this minimal set of cached entries (except if the relcache init file needs to be re-built, for example, after a DDL invalidates the cache). If disabled, each PostgreSQL connection preloads the catalog cache, which consumes more memory but reduces first query latency.

##### --ysql_yb_toast_catcache_threshold

{{% tags/wrap %}}

Default: `2048`
{{% /tags/wrap %}}

Specifies the threshold (in bytes) beyond which catalog tuples will get compressed when they are stored in the PostgreSQL catalog cache. Setting this flag reduces memory usage for certain large objects, including functions and views, in exchange for slower catalog refreshes.

To minimize performance impact when enabling this flag, set it to 2KB or higher.

##### --ysql_enable_db_catalog_version_mode

{{% tags/wrap %}}
{{<tags/feature/deprecated>}}
{{% /tags/wrap %}}

{{< warning title="Deprecated" >}}
Per-database catalog version mode is now mandatory. This flag is deprecated and has no effect; if set explicitly, a deprecation warning is logged at startup and the value is ignored.
{{< /warning >}}

In per-database catalog version mode, a DDL statement that affects the current database can only increment the catalog version for that database. As a result, multiple DDL statements can be concurrently executed if each DDL only affects its current database and is executed in a separate database. Existing connections only need to refresh their catalog caches if they are connected to the same database as that of a DDL statement.

##### --enable_heartbeat_pg_catalog_versions_cache

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

Whether to enable the use of heartbeat catalog versions cache for the
`pg_yb_catalog_version` table which can help to reduce the number of reads
from the table. This is beneficial when there are many databases and/or
many yb-tservers in the cluster.

{{< note title="Important" >}}

Each YB-TServer regularly sends a heartbeat request to the YB-Master
leader. As part of the heartbeat response, YB-Master leader reads all the rows
in the table `pg_yb_catalog_version` and sends the result back in the heartbeat
response. As there is one row in the table `pg_yb_catalog_version` for each
database, the cost of reading `table pg_yb_catalog_version` becomes more
expensive when the number of YB-TServers, or the number of databases goes up.

{{< /note >}}

##### --ysql_yb_enable_invalidation_messages

{{% tags/wrap %}}

Default: `true`
{{% /tags/wrap %}}

Enables YSQL backends to generate and consume invalidation messages incrementally for schema changes. When enabled (true), invalidation messages are propagated via the `pg_yb_invalidation_messages` per-database catalog table. Details of the invalidation messages generated by a DDL are also logged when [ysql_log_min_messages](../yb-tserver/#ysql-log-min-messages) is set to `DEBUG1` or when `yb_debug_log_catcache_events` is set to true. When disabled, schema changes cause a full catalog cache refresh on existing backends, which can result in a latency and memory spike on existing YSQL backends.


## Performance tuning

### Memory division flags

These flags are used to determine how the RAM of a node is split between the [TServer](../../../architecture/key-concepts/#tserver) and other processes, including the PostgreSQL processes and a [Master](../../../architecture/key-concepts/#master-server) process if present, as well as how to split memory inside of a TServer between various internal components like the RocksDB block cache.

{{< warning title="Do not oversubscribe memory" >}}

Ensure you do not oversubscribe memory when changing these flags.

When reserving memory for TServer and Master (if present), you must leave enough memory on the node for PostgreSQL, any required other processes like monitoring agents, and the memory needed by the kernel.

{{< /warning >}}

#### Flags controlling the defaults for the other memory division flags

The memory division flags have multiple sets of defaults; which set of defaults is in force depends on these flags.  Note that these defaults can differ between TServer and Master.

##### --use_memory_defaults_optimized_for_ysql

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

When creating a new universe using yugabyted or YugabyteDB Anywhere, the flag is set to `true`.

If true, the defaults for the memory division settings take into account the amount of RAM and cores available and are optimized for using YSQL. If false, the defaults will be the old defaults, which are more suitable for YCQL but do not take into account the amount of RAM and cores available.

For information on how memory is divided among processes when this flag is set, refer to [Memory division defaults](../../configuration/smart-defaults/).

#### Flags controlling the split of memory among processes

Note that in general these flags will have different values for TServer and Master processes.

##### --memory_limit_hard_bytes

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `0`
{{% /tags/wrap %}}

Maximum amount of memory this process should use in bytes, that is, its hard memory limit.  A value of `0` specifies to instead use a percentage of the total system memory; see [--default_memory_limit_to_ram_ratio](#default-memory-limit-to-ram-ratio) for the percentage used.  A value of `-1` disables all memory limiting.

For Kubernetes deployments, this flag is automatically set from the Kubernetes pod memory limits specified in the Helm chart configuration. See [Memory limits for Kubernetes deployments](../../../deploy/kubernetes/single-zone/oss/helm-chart/#memory-limits-for-kubernetes-deployments) for details.

##### --default_memory_limit_to_ram_ratio

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `-1000` (use the built-in default ratio; commonly `0.10` when [--use_memory_defaults_optimized_for_ysql](#use-memory-defaults-optimized-for-ysql) is false).
{{% /tags/wrap %}}

The percentage of available RAM to use for this process if [--memory_limit_hard_bytes](#memory-limit-hard-bytes) is `0`. The special value `-1000` selects that built-in default. Available RAM excludes memory reserved by the kernel.

This flag does not apply to Kubernetes universes. Memory limits are controlled via Kubernetes resource specifications in the Helm chart, and `--memory_limit_hard_bytes` is automatically set from those limits. See [Memory limits for Kubernetes deployments](../../../deploy/kubernetes/single-zone/oss/helm-chart/#memory-limits-for-kubernetes-deployments) for details.

#### Flags controlling the split of memory within a Master

These settings control the division of memory available to the Master process.

##### --db_block_cache_size_bytes

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `-1`
{{% /tags/wrap %}}

Size of the shared RocksDB block cache (in bytes).  A value of `-1` specifies to instead use a percentage of this processes' hard memory limit; see [--db_block_cache_size_percentage](#db-block-cache-size-percentage) for the percentage used.  A value of `-2` disables the block cache.

##### --db_block_cache_size_percentage

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `-1000` (use the built-in default percentage; commonly `25` when [--use_memory_defaults_optimized_for_ysql](#use-memory-defaults-optimized-for-ysql) is false).
{{% /tags/wrap %}}

Percentage of the process' hard memory limit to use for the shared RocksDB block cache if [--db_block_cache_size_bytes](#db-block-cache-size-bytes) is `-1`. The special value `-1000` means to use the built-in default for this flag. The special value `-3` means to use an older default that does not take the amount of RAM into account.

##### --tablet_overhead_size_percentage

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `-1000` (use the built-in recommended value; commonly `0` when [--use_memory_defaults_optimized_for_ysql](#use-memory-defaults-optimized-for-ysql) is false).
{{% /tags/wrap %}}

Percentage of the process' hard memory limit to use for tablet-related overheads. A value of `0` means no limit.  Must be between `0` and `100` inclusive. Exception: `-1000` specifies to instead use the default value for this flag.

Each tablet replica generally requires 700 MiB of this memory.

### Raft and consistency/timing flags

With the exception of flags that have different defaults for yb-master vs yb-tserver (for example, `--evict_failed_followers`), for a typical deployment, values used for Raft-related flags in yb-master configurations should match the values in [yb-tserver](../yb-tserver/#raft-and-consistency-timing-flags) configurations.

For WAL settings, see [Write ahead log (WAL) flags](#write-ahead-log-wal-flags).

##### --follower_unavailable_considered_failed_sec

{{% tags/wrap %}}
Default: `7200` (2 hours)
{{% /tags/wrap %}}

The duration, in seconds, after which a follower is considered to be failed because the leader has not received a heartbeat.

The `--follower_unavailable_considered_failed_sec` value should match the value for [--log_min_seconds_to_retain](#log-min-seconds-to-retain).

##### --evict_failed_followers

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

Failed followers will be evicted from the Raft group and the data will be re-replicated.

Note that it is not recommended to set the flag to true for Masters as you cannot automatically recover a failed Master once it is evicted.

##### --leader_failure_max_missed_heartbeat_periods

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `6`
{{% /tags/wrap %}}

The maximum heartbeat periods that the leader can fail to heartbeat in before the leader is considered to be failed. The total failure timeout, in milliseconds (ms), is [--raft_heartbeat_interval_ms](#raft-heartbeat-interval-ms) multiplied by `--leader_failure_max_missed_heartbeat_periods`.

For read replica clusters, set the value to `10` in all yb-tserver and yb-master configurations.  Because the data is globally replicated, RPC latencies are higher. Use this flag to increase the failure detection interval in such a higher RPC latency deployment.

##### --leader_lease_duration_ms

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `2000`
{{% /tags/wrap %}}

The leader lease duration, in milliseconds. A leader keeps establishing a new lease or extending the existing one with every consensus update. A new server is not allowed to serve as a leader (that is, serve up-to-date read requests or acknowledge write requests) until a lease of this duration has definitely expired on the old leader's side, or the old leader has explicitly acknowledged the new leader's lease.

This lease allows the leader to safely serve reads for the duration of its lease, even during a network partition. For more information, refer to [Leader leases](../../../architecture/transactions/single-row-transactions/#leader-leases-reading-the-latest-data-in-case-of-a-network-partition).

Leader lease duration should be longer than the heartbeat interval, and less than the multiple of `--leader_failure_max_missed_heartbeat_periods` multiplied by `--raft_heartbeat_interval_ms`.

##### --raft_heartbeat_interval_ms

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `500`
{{% /tags/wrap %}}

The heartbeat interval, in milliseconds (ms), for Raft replication. The leader produces heartbeats to followers at this interval. The followers expect a heartbeat at this interval and consider a leader to have failed if it misses several in a row.

### RocksDB and compaction/performance flags

Use the following two flags to select the SSTable compression type:

##### --enable_ondisk_compression

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `true`
{{% /tags/wrap %}}

Enable SSTable compression at the cluster level.

##### --compression_type

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `Snappy`
{{% /tags/wrap %}}

Change the SSTable compression type. The valid compression types are `Snappy`, `Zlib`, `LZ4`, and `NoCompression`.

If you select an invalid option, the cluster will not come up.

If you change this flag, the change takes effect after you restart the cluster nodes.

Changing this flag on an existing database is supported; a tablet can validly have SSTs with different compression types. Eventually, compaction will remove the old compression type files.

##### --regular_tablets_data_block_key_value_encoding

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `shared_prefix`
{{% /tags/wrap %}}

Key-value encoding to use for regular data blocks in RocksDB. Possible options: `shared_prefix`, `three_shared_parts`.

Only change this flag to `three_shared_parts` after you migrate the whole cluster to the YugabyteDB version that supports it.

##### --rocksdb_compact_flush_rate_limit_bytes_per_sec

{{% tags/wrap %}}

Default: `1073741824`
{{% /tags/wrap %}}

Used to control rate of memstore flush and SSTable file compaction.

##### --rocksdb_universal_compaction_min_merge_width

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `4`
{{% /tags/wrap %}}

Compactions run only if there are at least `rocksdb_universal_compaction_min_merge_width` eligible files and their running total (summation of size of files considered so far) is within `rocksdb_universal_compaction_size_ratio` of the next file in consideration to be included into the same compaction.

##### --rocksdb_max_background_compactions

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `-1`
{{% /tags/wrap %}}

Maximum number of threads to do background compactions (used when compactions need to catch up.) Unless `rocksdb_disable_compactions=true`, this cannot be set to zero.

The default of `-1` means that the value is calculated at runtime as follows:

- For servers with up to 4 CPU cores, the default value is considered as `1`.
- For servers with up to 8 CPU cores, the default value is considered as `2`.
- For servers with up to 32 CPU cores, the default value is considered as `3`.
- Beyond 32 cores, the default value is considered as `4`.

##### --rocksdb_compaction_size_threshold_bytes

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `2147483648`
{{% /tags/wrap %}}

Threshold beyond which a compaction is considered large.

##### --rocksdb_level0_file_num_compaction_trigger

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `5`.
{{% /tags/wrap %}}

Number of files to trigger level-0 compaction. Set to `-1` if compaction should not be triggered by number of files at all.

##### --rocksdb_universal_compaction_size_ratio

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `20`
{{% /tags/wrap %}}

Compactions run only if there are at least `rocksdb_universal_compaction_min_merge_width` eligible files and their running total (summation of size of files considered so far) is within `rocksdb_universal_compaction_size_ratio` of the next file in consideration to be included into the same compaction.

##### --timestamp_history_retention_interval_sec

{{% tags/wrap %}}

Default: `900` (15 minutes)
{{% /tags/wrap %}}

The time interval, in seconds, to retain history/older versions of data. Point-in-time reads at a hybrid time prior to this interval might not be allowed after a compaction and return a `Snapshot too old` error. Set this to be greater than the expected maximum duration of any single transaction in your application.

##### --remote_bootstrap_rate_limit_bytes_per_sec

{{% tags/wrap %}}

Default: `268435456`
{{% /tags/wrap %}}

Rate control across all tablets being remote bootstrapped from or to this process.

##### --remote_bootstrap_from_leader_only

{{% tags/wrap %}}

Default: `false`
{{% /tags/wrap %}}

Based on the value (`true`/`false`) of the flag, the leader decides whether to instruct the new peer to attempt bootstrap from a closest caught-up peer. The leader too could be the closest peer depending on the new peer's geographic placement. Setting the flag to false will enable the feature of remote bootstrapping from a closest caught-up peer. The number of bootstrap attempts from a non-leader peer is limited by the flag [max_remote_bootstrap_attempts_from_non_leader](#max-remote-bootstrap-attempts-from-non-leader).

{{< note title="Note" >}}

The code for the feature is present from version 2.16 and later, and can be enabled explicitly if needed. Starting from version 2.19, the feature is on by default.

{{< /note >}}

##### --max_remote_bootstrap_attempts_from_non_leader

{{% tags/wrap %}}

Default: `5`
{{% /tags/wrap %}}

When the flag [remote_bootstrap_from_leader_only](#remote-bootstrap-from-leader-only) is set to `false` (enabling the feature of bootstrapping from a closest peer), the number of attempts where the new peer tries to bootstrap from a non-leader peer is limited by the flag. After these failed bootstrap attempts for the new peer, the leader peer sets itself as the bootstrap source.

##### --db_block_cache_num_shard_bits

{{% tags/wrap %}}

Default: `-1`
{{% /tags/wrap %}}

`-1` indicates a dynamic scheme that evaluates to 4 if number of cores is less than or equal to 16, 5 for 17-32 cores, 6 for 33-64 cores, and so on.

{{< note title="Note" >}}

Starting from version 2.18, the default is `-1`. Previously it was `4`.

{{< /note >}}

##### --full_compaction_pool_max_threads

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `1`
{{% /tags/wrap %}}

The maximum number of threads allowed for non-admin full compactions. This includes post-split compactions (compactions that remove irrelevant data from new tablets after splits) and scheduled full compactions.

##### --auto_compact_check_interval_sec

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `60`
{{% /tags/wrap %}}

The interval at which the full compaction task will check for tablets eligible for compaction (both for the statistics-based full compaction and scheduled full compaction features). `0` indicates that the statistics-based full compactions feature is disabled.

##### --auto_compact_stat_window_seconds

{{% tags/wrap %}}

Default: `300`
{{% /tags/wrap %}}

Window of time in seconds over which DocDB read statistics are analyzed for the purpose of triggering full compactions to improve read performance. Both [auto_compact_percent_obsolete](#auto-compact-percent-obsolete) and [auto_compact_min_obsolete_keys_found](#auto-compact-min-obsolete-keys-found) are evaluated over this period of time.

`auto_compact_stat_window_seconds` must be evaluated as a multiple of [auto_compact_check_interval_sec](#auto-compact-check-interval-sec), and will be rounded up to meet this constraint. For example, if `auto_compact_stat_window_seconds` is set to `100` and `auto_compact_check_interval_sec` is set to `60`, it will be rounded up to `120` at runtime.

##### --auto_compact_percent_obsolete

{{% tags/wrap %}}

Default: `99`
{{% /tags/wrap %}}

The percentage of obsolete keys (over total keys) read over the [auto_compact_stat_window_seconds](#auto-compact-stat-window-seconds) window of time required to trigger an automatic full compaction on a tablet. Only keys that are past their history retention (and thus can be garbage collected) are counted towards this threshold.

For example, if the flag is set to `99` and 100000 keys are read over that window of time, and 99900 of those are obsolete and past their history retention, a full compaction will be triggered (subject to other conditions).

##### --auto_compact_min_obsolete_keys_found

{{% tags/wrap %}}

Default: `10000`
{{% /tags/wrap %}}

Minimum number of keys that must be read over the last [auto_compact_stat_window_seconds](#auto-compact-stat-window-seconds) to trigger a statistics-based full compaction.

##### --auto_compact_min_wait_between_seconds

{{% tags/wrap %}}

Default: `0`
{{% /tags/wrap %}}

Minimum wait time between statistics-based and scheduled full compactions. To be used if statistics-based compactions are triggering too frequently.

##### --scheduled_full_compaction_frequency_hours

{{% tags/wrap %}}

Default: `0`
{{% /tags/wrap %}}

The frequency with which full compactions should be scheduled on tablets. `0` indicates that the feature is disabled. Recommended value: `720` hours or greater (that is, 30 days).

##### --scheduled_full_compaction_jitter_factor_percentage

{{% tags/wrap %}}

Default: `33`
{{% /tags/wrap %}}

Percentage of [scheduled_full_compaction_frequency_hours](#scheduled-full-compaction-frequency-hours) to be used as jitter when determining full compaction schedule per tablet. Must be a value between `0` and `100`. Jitter is introduced to prevent many tablets from being scheduled for full compactions at the same time.

Jitter is deterministically computed when scheduling a compaction, between 0 and (frequency * jitter factor) hours. Once computed, the jitter is subtracted from the intended compaction frequency to determine the tablet's next compaction time.

Example: If `scheduled_full_compaction_frequency_hours` is `720` hours (that is, 30 days), and `scheduled_full_compaction_jitter_factor_percentage` is `33` percent, each tablet will be scheduled for compaction every `482` hours to `720` hours.

##### --automatic_compaction_extra_priority

{{% tags/wrap %}}

Default: `50`
{{% /tags/wrap %}}

Assigns an extra priority to automatic (minor) compactions when automatic tablet splitting is enabled. This deprioritizes post-split compactions and ensures that smaller compactions are not starved. Suggested values are between 0 and 50.

### Concurrency control flags

To learn about Wait-on-Conflict concurrency control, see [Concurrency control](../../../architecture/transactions/concurrency-control/).

##### --enable_wait_queues

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `true`
{{% /tags/wrap %}}

When set to true, enables in-memory wait queues, deadlock detection, and wait-on-conflict semantics in all YSQL traffic.

##### --disable_deadlock_detection

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

When set to true, disables deadlock detection. If `enable_wait_queues=false`, this flag has no effect as deadlock detection is not running anyways.

{{< warning title="Warning">}}
Use of this flag can potentially result in deadlocks that can't be resolved by YSQL. Use this flag only if the application layer can guarantee deadlock avoidance.
{{< /warning >}}

##### --wait_queue_poll_interval_ms

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `100`
{{% /tags/wrap %}}

If `enable_wait_queues=true`, this controls the rate at which each tablet's wait queue polls transaction coordinators for the status of transactions which are blocking contentious resources.

### DDL concurrency flags

##### --ysql_enable_db_catalog_version_mode

{{% tags/wrap %}}
{{<tags/feature/deprecated>}}
{{% /tags/wrap %}}

{{< warning title="Deprecated" >}}
Per-database catalog version mode is now mandatory. This flag is deprecated and has no effect; if set explicitly, a deprecation warning is logged at startup and the value is ignored.
{{< /warning >}}

In per-database catalog version mode, a DDL statement that affects the current database can only increment the catalog version for that database. As a result, multiple DDL statements can be concurrently executed if each DDL only affects its current database and is executed in a separate database. Existing connections only need to refresh their catalog caches if they are connected to the same database as that of a DDL statement.

##### --enable_heartbeat_pg_catalog_versions_cache

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

Whether to enable the use of heartbeat catalog versions cache for the
`pg_yb_catalog_version` table which can help to reduce the number of reads
from the table. This is beneficial when there are many databases and/or
many yb-tservers in the cluster.

{{< note title="Important" >}}

Each YB-TServer regularly sends a heartbeat request to the YB-Master
leader. As part of the heartbeat response, YB-Master leader reads all the rows
in the table `pg_yb_catalog_version` and sends the result back in the heartbeat
response. As there is one row in the table `pg_yb_catalog_version` for each
database, the cost of reading `table pg_yb_catalog_version` becomes more
expensive when the number of YB-TServers, or the number of databases goes up.

{{< /note >}}

### Cost-based optimizer flag

Configure the YugabyteDB [cost-based optimizer](../../../architecture/query-layer/planner-optimizer/) (CBO).

See also the [yb_enable_cbo](#yb-enable-cbo) configuration parameter. If this flag is set, the parameter takes precedence.

##### --ysql_yb_enable_cbo

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `legacy_mode`
{{% /tags/wrap %}}

Enables the YugabyteDB [cost-based optimizer](../../../architecture/query-layer/planner-optimizer/) (CBO). Options are `on`, `off`, `legacy_mode`, and `legacy_stats_mode`.

In v2025.2 and later, CBO is enabled ('on') by default in new universes when you deploy using yugabyted, YugabyteDB Anywhere, or YugabyteDB Aeon.

When CBO is enabled (set to `on`), [auto analyze](#auto-analyze-service-flags) is also enabled automatically. If you disable auto analyze explicitly, you are responsible for periodically running ANALYZE on user tables to maintain up-to-date statistics.

For information on using this parameter to configure CBO, refer to [Enable cost-based optimizer](../../../best-practices-operations/ysql-yb-enable-cbo/).

### Auto Analyze service flags

To learn about the Auto Analyze service, see [Auto Analyze service](../../../additional-features/auto-analyze/).

Auto analyze is automatically enabled when the [cost-based optimizer](../../../architecture/query-layer/planner-optimizer/) (CBO) is enabled by setting the [yb_enable_cbo](../yb-tserver/#yb_enable_cbo) flag to `on`.

##### ysql_enable_auto_analyze_service (deprecated)

{{% tags/wrap %}}

Default: `false`
{{% /tags/wrap %}}

Use [ysql_enable_auto_analyze](../yb-tserver/#ysql_enable_auto_analyze) on yb-tservers instead.

### Advisory lock flags

To learn about advisory locks, see [Advisory locks](../../../architecture/transactions/concurrency-control/#advisory-locks).

##### --ysql_yb_enable_advisory_locks

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
Default: `true`
{{% /tags/wrap %}}

Enables advisory locking.

This value must match on all YB-Master and YB-TServer configurations of a YugabyteDB cluster.

### Index backfill flags

##### --defer_index_backfill

{{% tags/wrap %}}

Default: `false`
{{% /tags/wrap %}}

If enabled, YB-Master avoids launching any new index-backfill jobs on the cluster for all new YCQL indexes.

You will need to run [yb-admin backfill_indexes_for_table](../../../admin/yb-admin/#backfill-indexes-for-table) manually for indexes to be functional.
See [CREATE DEFERRED INDEX](../../../api/ycql/ddl_create_index/#deferred-index) for reference.

##### --allow_batching_non_deferred_indexes

{{% tags/wrap %}}

Default: `true`
{{% /tags/wrap %}}

If enabled, indexes on the same (YCQL) table may be batched together during backfill, even if they were not deferred.

##### --ysql_index_backfill_rpc_timeout_ms

{{% tags/wrap %}}

Default: `300000`
{{% /tags/wrap %}}

Deadline (in milliseconds) for each internal YB-Master to YB-TServer RPC for backfilling a chunk of the index.

### Other performance tuning options

##### --allowed_preview_flags_csv

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
{{% /tags/wrap %}}

Comma-separated values (CSV) formatted catalogue of [preview feature](/stable/releases/versioning/#tech-preview-tp) flag names. Preview flags represent experimental or in-development features that are not yet fully supported. Flags that are tagged as "preview" cannot be modified or configured unless they are included in this list.

By adding a flag to this list, you explicitly acknowledge and accept any potential risks or instability that may arise from modifying these preview features. This process serves as a safeguard, ensuring that you are fully aware of the experimental nature of the flags you are working with.

{{<warning title="You still need to set the flag">}}
Adding flags to this list doesn't automatically change any settings. It only _grants permission_ for the flag to be modified.

You still need to configure the flag separately after adding it to this list.
{{</warning>}}

{{<note title="Using YugabyteDB Anywhere">}}
If you are using YugabyteDB Anywhere, as with other flags, set `allowed_preview_flags_csv` using the [Edit Flags](../../../yugabyte-platform/manage-deployments/edit-config-flags/#modify-configuration-flags) feature.

After adding a preview flag to the `allowed_preview_flags_csv` list, you still need to set the flag using **Edit Flags** as well.
{{</note>}}

##### --hide_dead_node_threshold_mins

{{% tags/wrap %}}

Default: `1440` (1 day)
{{% /tags/wrap %}}

Number of minutes to wait before no longer displaying a dead node (no heartbeat) in the [YB-Master Admin UI](#admin-ui) (the node is presumed to have been removed from the cluster).

##### --ysql_enable_write_pipelining

{{% tags/wrap %}}
{{<tags/feature/ea idea="1298">}}
{{<tags/feature/restart-needed>}}
{{% tags/feature/t-server %}}
Default: `false`
{{% /tags/wrap %}}

Enables concurrent replication of multiple write operations in a transaction. Write requests to DocDB return immediately after completing on the leader, meanwhile the Raft quorum commit happens asynchronously in the background. This enables PostgreSQL to be able to send the next write or read request in parallel, which reduces overall latency. Note that this does not affect the transactional guarantees of the system. The COMMIT of the transaction waits and ensures all asynchronous quorum replication has completed.

Note that this is a preview flag, so it also needs to be added to the [allowed_preview_flags_csv](#allowed-preview-flags-csv) list.

This flag also needs to be enabled on [YB-TServer servers](../yb-tserver/#ysql_enable_write_pipelining).


## Security

### Security flags for encryption and certificates

For details on enabling encryption in transit, see [Encryption in transit](../../../secure/tls-encryption/).

##### --certs_dir

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `""` (uses `<data drive>/yb-data/master/data/certs`.)
{{% /tags/wrap %}}

Directory that contains certificate authority, private key, and certificates for this server.

##### --certs_for_client_dir

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `""` (Use the same directory as certs_dir.)
{{% /tags/wrap %}}

The directory that contains certificate authority, private key, and certificates for this server that should be used for client-to-server communications.

##### --allow_insecure_connections

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `true`
{{% /tags/wrap %}}

Allow insecure connections. Set to `false` to prevent any process with unencrypted communication from joining a cluster. Note that this flag requires [use_node_to_node_encryption](#use-node-to-node-encryption) to be enabled and [use_client_to_server_encryption](#use-client-to-server-encryption) to be enabled.

##### --dump_certificate_entries

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

Adds certificate entries, including IP addresses and hostnames, to log for handshake error messages. Enable this flag to debug certificate issues.

##### --use_client_to_server_encryption

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

Use client-to-server (client-to-node) encryption to protect data in transit between YugabyteDB servers and clients, tools, and APIs.

##### --use_node_to_node_encryption

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

Enables server-server (node-to-node) encryption between YB-Master and YB-TServer servers in a cluster or universe. To work properly, all YB-TServer servers must also have their [--use_node_to_node_encryption](../yb-tserver/#use-node-to-node-encryption) flag enabled.

When enabled, [--allow_insecure_connections](#allow-insecure-connections) should be set to false to disallow insecure connections.

##### --cipher_list

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `""` (empty string; library defaults apply)
{{% /tags/wrap %}}

Specify cipher lists for TLS 1.2 and earlier versions. (For TLS 1.3, use [--ciphersuites](#ciphersuites).) Use a colon-separated list of TLS 1.2 cipher names in order of preference. Use an exclamation mark ( `!` ) to exclude ciphers. For example:

```sh
--cipher_list DEFAULTS:!DES:!IDEA:!3DES:!RC2
```

This allows all ciphers for TLS 1.2 to be accepted, except those matching the category of ciphers omitted.

This flag requires a restart or rolling restart.

For more information, refer to [SSL_CTX_set_cipher_list](https://www.openssl.org/docs/man1.1.1/man3/SSL_CTX_set_cipher_list.html) in the OpenSSL documentation.

##### --ciphersuites

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `""` (empty string; library defaults apply)
{{% /tags/wrap %}}

Define the available TLS 1.3 ciphersuites. For TLS 1.2 and earlier, use [--cipher_list](#cipher-list).

Use a colon-separated list of TLS 1.3 ciphersuite names in order of preference. Use an exclamation mark ( ! ) to exclude ciphers. For example:

```sh
--ciphersuites DEFAULTS:!CHACHA20
```

This allows all ciphersuites for TLS 1.3 to be accepted, except CHACHA20 ciphers.

This flag requires a restart or rolling restart.

For more information, refer to [SSL_CTX_set_cipher_list](https://www.openssl.org/docs/man1.1.1/man3/SSL_CTX_set_cipher_list.html) in the OpenSSL documentation.

##### --ssl_protocols

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: An empty string, which is equivalent to allowing all protocols except "ssl2" and "ssl3".
{{% /tags/wrap %}}

Specifies an explicit allow-list of TLS protocols for YugabyteDB's internal RPC communication.

You can pass a comma-separated list of strings, where the strings can be one of "ssl2", "ssl3", "tls10", "tls11", "tls12", and "tls13".

You can set the TLS version for node-to-node and client-node communication. To enforce TLS 1.2, set the flag to tls12 as follows:

```sh
--ssl_protocols = tls12
```

To specify a _minimum_ TLS version of 1.2, for example, the flag needs to be set to tls12, tls13, and all available subsequent versions.

```sh
--ssl_protocols = tls12,tls13
```

By default, PostgreSQL uses a default minimum version for TLS of v1.2, as set using the [ssl_min_protocol_version](https://www.postgresql.org/docs/15/runtime-config-connection.html#GUC-SSL-MIN-PROTOCOL-VERSION) configuration parameter.

As the `ssl_protocols` setting does not propagate to PostgreSQL, if you specify a different minimum TLS version for Master and TServer, you should update the `ssl_min_protocol_version` parameter. For example:

```sh
--ysql_pg_conf_csv="ssl_min_protocol_version='TLSv1.3'"
```

### Authentication and authorization flags

#### YSQL

The following flags support the use of the [YSQL API](../../../api/ysql/):

##### --enable_ysql

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
{{<tags/feature/restart-needed>}}
Default: `true`
{{% /tags/wrap %}}

Enables the YSQL API when value is `true`.

##### --ysql_enable_auth

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

Enables YSQL authentication.

When YSQL authentication is enabled, you can sign into ysqlsh using the default `yugabyte` user that has a default password of `yugabyte`.

##### --ysql_enable_profile

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

Enables YSQL [login profiles](../../../secure/enable-authentication/ysql-login-profiles/).

When YSQL login profiles are enabled, you can set limits on the number of failed login attempts made by users.

##### --pgsql_proxy_bind_address

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `0.0.0.0:5433`
{{% /tags/wrap %}}

Specifies the TCP/IP bind addresses for the YSQL API. The default value of `0.0.0.0:5433` allows listening for all IPv4 addresses access to localhost on port `5433`. The `--pgsql_proxy_bind_address` value overwrites `listen_addresses` (default value of `127.0.0.1:5433`) that controls which interfaces accept connection attempts.

To specify fine-grained access control over who can access the server, use [--ysql_hba_conf](#ysql-hba-conf).

##### --pgsql_proxy_webserver_port

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `13000`
{{% /tags/wrap %}}

Specifies the web server port for YSQL metrics monitoring.

##### --ysql_hba_conf

{{% tags/wrap %}}{{<tags/feature/deprecated>}}{{<tags/feature/restart-needed>}}{{% /tags/wrap %}}

Deprecated. Use `--ysql_hba_conf_csv` instead.

##### --ysql_hba_conf_csv

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `"host all all 0.0.0.0/0 trust,host all all ::0/0 trust"`
{{% /tags/wrap %}}

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

##### --ysql_pg_conf

{{% tags/wrap %}}{{<tags/feature/deprecated>}}{{<tags/feature/restart-needed>}}{{% /tags/wrap %}}

Deprecated. Use `--ysql_pg_conf_csv` instead.

##### --ysql_pg_conf_csv

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
{{% /tags/wrap %}}

Comma-separated list of PostgreSQL server configuration parameters that is appended to the `postgresql.conf` file. If internal quotation marks are required, surround each configuration pair having single quotation marks with double quotation marks.

For example:

```sh
--ysql_pg_conf_csv="log_line_prefix='%m [%p %l %c] %q[%C %R %Z %H] [%r %a %u %d] '","pgaudit.log='all, -misc'",pgaudit.log_parameter=on,pgaudit.log_relation=on,pgaudit.log_catalog=off,suppress_nonpg_logs=on
```

For information on available PostgreSQL server configuration parameters, refer to [Server Configuration](https://www.postgresql.org/docs/15/runtime-config.html) in the PostgreSQL documentation.

The configuration parameters for YugabyteDB are the same as for PostgreSQL, with some minor exceptions. Refer to [Configuration parameters](../yb-tserver/#postgresql-configuration-parameters).

##### --ysql_timezone

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: Uses the YSQL time zone.
{{% /tags/wrap %}}

Specifies the time zone for displaying and interpreting timestamps.

##### --ysql_datestyle

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: Uses the ISO, MDY display format.
{{% /tags/wrap %}}

Specifies the display format for data and time values.

##### --ysql_max_connections

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: If `ysql_max_connections` is not set, the database startup process will determine the highest number of connections the system can support, from a minimum of 50 to a maximum of 300 (per node).
{{% /tags/wrap %}}

Specifies the maximum number of concurrent YSQL connections per node.

This is a maximum per server, so a 3-node cluster will have a default of 900 available connections, globally.

Any active, idle in transaction, or idle in session connection counts toward the connection limit.

Some connections are reserved for superusers. The total number of superuser connections is determined by the `superuser_reserved_connections` [configuration parameter](../yb-tserver/#postgresql-configuration-parameters). Connections available to non-superusers is equal to `ysql_max_connections` - `superuser_reserved_connections`.

##### --ysql_default_transaction_isolation

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `'read committed'`
{{% /tags/wrap %}}

Specifies the default transaction isolation level.

Valid values: `serializable`, `'repeatable read'`, `'read committed'`, and `'read uncommitted'`.

[Read Committed isolation](../../../explore/transactions/isolation-levels/) is supported only if the YB-Master flag `yb_enable_read_committed_isolation` is set to `true`.

For new universes running v2025.2 or later, `yb_enable_read_committed_isolation` is set to `true` by default when you deploy using yugabyted, YugabyteDB Anywhere, or YugabyteDB Aeon.

If `yb_enable_read_committed_isolation` is `false`, the Read Committed isolation level of the YugabyteDB transactional layer falls back to the stricter Snapshot isolation (in which case Read Committed and Read Uncommitted of YSQL also in turn use Snapshot isolation).

##### --yb_enable_read_committed_isolation

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

Enables Read Committed isolation.

For new universes running v2025.2 or later, `yb_enable_read_committed_isolation` is set to `true` by default when you deploy using yugabyted, YugabyteDB Anywhere, or YugabyteDB Aeon.

When set to false, Read Committed (and Read Uncommitted) isolation level of YSQL fall back to the stricter [Snapshot isolation](../../../explore/transactions/isolation-levels/). See also the [--ysql_default_transaction_isolation](#ysql-default-transaction-isolation) flag.

##### --pg_client_use_shared_memory

{{% tags/wrap %}}
Default: `true`
{{% /tags/wrap %}}

Enables the use of shared memory between PostgreSQL and the YB-TServer. Using shared memory can potentially improve the performance of your database operations.

##### --ysql_sequence_cache_method

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `connection`
{{% /tags/wrap %}}

Specifies where to cache sequence values.

Valid values are `connection` and `server`.

This flag requires the YB-TServer `yb_enable_sequence_pushdown` flag to be true (the default). Otherwise, the default behavior will occur regardless of this flag's value.

For details on caching values on the server and switching between cache methods, see the semantics on the [nextval](../../../api/ysql/exprs/sequence_functions/func_nextval/) page.

##### --ysql_sequence_cache_minval

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `100`
{{% /tags/wrap %}}

Specifies the minimum number of sequence values to cache in the client for every sequence object.

To turn off the default size of cache flag, set the flag to `0`.

For details on the expected behaviour when used with the sequence cache clause, see the semantics under [CREATE SEQUENCE](../../../api/ysql/the-sql-language/statements/ddl_create_sequence/#cache-cache) and [ALTER SEQUENCE](../../../api/ysql/the-sql-language/statements/ddl_alter_sequence/#cache-cache) pages.

##### --ysql_yb_fetch_size_limit

{{% tags/wrap %}}

Default: `0`
{{% /tags/wrap %}}

Specifies the maximum size (in bytes) of total data returned in one response when the query layer fetches rows of a table from DocDB. Used to bound how many rows can be returned in one request. Set to 0 to have no size limit.

You can also specify the value as a string. For example, you can set it to `'10MB'`.

You should have at least one of row limit or size limit set.

If both `--ysql_yb_fetch_row_limit` and `--ysql_yb_fetch_size_limit` are greater than zero, then limit is taken as the lower bound of the two values.

See also the [yb_fetch_size_limit](../yb-tserver/#yb-fetch-size-limit) configuration parameter. If both flag and parameter are set, the parameter takes precedence.

##### --ysql_yb_fetch_row_limit

{{% tags/wrap %}}

Default: `1024`
{{% /tags/wrap %}}

Specifies the maximum number of rows returned in one response when the query layer fetches rows of a table from DocDB. Used to bound how many rows can be returned in one request. Set to 0 to have no row limit.

You should have at least one of row limit or size limit set.

If both `--ysql_yb_fetch_row_limit` and `--ysql_yb_fetch_size_limit` are greater than zero, then limit is taken as the lower bound of the two values.

See also the [yb_fetch_row_limit](../yb-tserver/#yb-fetch-row-limit) configuration parameter. If both flag and parameter are set, the parameter takes precedence.

##### --ysql_log_statement

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `none`
{{% /tags/wrap %}}

Specifies the types of YSQL statements that should be logged.

Valid values: `none` (off), `ddl` (only data definition queries, such as create/alter/drop), `mod` (all modifying/write statements, includes DDLs plus insert/update/delete/truncate, etc), and `all` (all statements).

##### --ysql_log_min_duration_statement

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `-1` (disables logging statement durations)
{{% /tags/wrap %}}

Logs the duration of each completed SQL statement that runs the specified duration (in milliseconds) or longer. Setting the value to `0` prints all statement durations. You can use this flag to help track down unoptimized (or "slow") queries.

##### --ysql_log_min_messages

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `WARNING`
{{% /tags/wrap %}}

Specifies the [severity level](https://www.postgresql.org/docs/15/runtime-config-logging.html#RUNTIME-CONFIG-SEVERITY-LEVELS) of messages to log.

##### --enable_pg_cron

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

Set this flag to true on all YB-Masters and YB-TServers to add the [pg_cron extension](../../../additional-features/pg-extensions/extension-pgcron/).

##### --ysql_cron_database_name

{{% tags/wrap %}}

Default: `yugabyte`
{{% /tags/wrap %}}

Specifies the database where pg_cron is to be installed. You can create the database after setting the flag.

The [pg_cron extension](../../../additional-features/pg-extensions/extension-pgcron/) is installed on only one database (by default, `yugabyte`).

To change the database after the extension is created, you must first drop the extension and then change the flag value.

##### --ysql_output_buffer_size

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `1048576`
{{% /tags/wrap %}}

Size of YSQL layer output buffer, in bytes. YSQL buffers query responses in this output buffer until either a buffer flush is requested by the client or the buffer overflows.

As long as no data has been flushed from the buffer, the database can retry queries on retryable errors. For example, you can increase the size of the buffer so that YSQL can retry [read restart errors](../../../architecture/transactions/read-restart-error/).

##### --ysql_yb_bnl_batch_size

{{% tags/wrap %}}

Default: `1024`
{{% /tags/wrap %}}

Sets the size of a tuple batch that's taken from the outer side of a [batched nested loop (BNL) join](../../../architecture/query-layer/join-strategies/#batched-nested-loop-join-bnl). When set to 1, BNLs are effectively turned off and won't be considered as a query plan candidate.

See also the [yb_bnl_batch_size](../yb-tserver/#yb-bnl-batch-size) configuration parameter. If both flag and parameter are set, the parameter takes precedence.

##### --ysql_follower_reads_avoid_waiting_for_safe_time

{{% tags/wrap %}}

Default: `true`
{{% /tags/wrap %}}

Controls whether YSQL follower reads that specify a not-yet-safe read time should be rejected. This will force them to go to the leader, which will likely be faster than waiting for safe time to catch up.

##### --enable_ysql_operation_lease

{{% tags/wrap %}}

Default: `true`
{{% /tags/wrap %}}

Enables the [YSQL lease mechanism](../../../architecture/transactions/concurrency-control/#ysql-lease-mechanism).

On YB-TServers, a new PostgreSQL process is spawned only after establishing a lease with the YB-Master leader. The flag also controls whether the background YB-TServer task makes lease refresh requests.

##### --ysql_lease_refresher_interval_ms

{{% tags/wrap %}}

Default: `1000` (1 second)
{{% /tags/wrap %}}

Determines the interval a YB-TServer waits before initiating another YSQL lease refresh RPC.

Refer to [YSQL lease mechanism](../../../architecture/transactions/concurrency-control/#ysql-lease-refresher-interval-ms) for more details.

##### --master_ysql_operation_lease_ttl_ms

{{% tags/wrap %}}

Default: `5 * 60 * 1000` (5 minutes)
{{% /tags/wrap %}}

Specifies base YSQL lease Time-To-Live (TTL). The YB-Master leader uses this value to determine the validity of a YB-TServer's YSQL lease.

Refer to [YSQL lease mechanism](../../../architecture/transactions/concurrency-control/#master-ysql-operation-lease-ttl-ms) for more details.

##### --ysql_operation_lease_ttl_client_buffer_ms

{{% tags/wrap %}}

Default: 2000 (2 seconds)
{{% /tags/wrap %}}

Specifies a client-side buffer for the YSQL operation lease TTL.

Refer to [YSQL lease mechanism](../../../architecture/transactions/concurrency-control/#ysql-operation-lease-ttl-client-buffer-ms) for more details.

<!--
## Admin UI

The Admin UI for YB-Master is available at <http://localhost:7000>.

### Home

Home page of the YB-Master server that gives a high level overview of the cluster. Not all YB-Master servers in a cluster show identical information.

![master-home](/images/admin/master-home-binary-with-tables.png)

### Namespaces

List of namespaces present in the cluster.

![master-namespaces](/images/admin/master-namespaces.png)

### Tables

List of tables present in the cluster.

![master-tables](/images/admin/master-tables.png)

### Tablet servers

List of all nodes (aka YB-TServer servers) present in the cluster.

![master-tservers](/images/admin/master-tservers-list-binary-with-tablets.png)

### Debug

List of all utilities available to debug the performance of the cluster.

![master-debug](/images/admin/master-debug.png)
-->
