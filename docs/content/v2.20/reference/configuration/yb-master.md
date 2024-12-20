---
title: yb-master configuration reference
headerTitle: yb-master
linkTitle: yb-master
description: YugabyteDB Master Server (yb-master) binary and configuration flags to manage cluster metadata and coordinate cluster-wide operations.
menu:
  v2.20:
    identifier: yb-master
    parent: configuration
    weight: 2450
type: docs
---

Use the `yb-master` binary and its flags to configure the [YB-Master](../../../architecture/concepts/yb-master/) server. The `yb-master` executable file is located in the `bin` directory of YugabyteDB home.

## Syntax

```sh
yb-master [ flag  ] | [ flag ]
```

### Example

```sh
./bin/yb-master \
--master_addresses 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
--rpc_bind_addresses 172.151.17.130 \
--fs_data_dirs "/home/centos/disk1,/home/centos/disk2" \
--replication_factor=3
```

### Online help

To display the online help, run `yb-master --help` from the YugabyteDB home directory:

```sh
./bin/yb-master --help
```

## All flags

The following sections describe the flags considered relevant to configuring YugabyteDB for production deployments. For a list of all flags, see [All YB-Master flags](../all-flags-yb-master/).

## General flags

##### --version

Shows version and build information, then exits.

##### --flagfile

Specifies the configuration file to load flags from.

##### --master_addresses

Specifies a comma-separated list of all RPC addresses for `yb-master` consensus-configuration.

{{< note title="Note" >}}

The number of comma-separated values should match the total number of YB-Master server (or the replication factor).

{{< /note >}}

Required.

Default: `127.0.0.1:7100`

##### --fs_data_dirs

Specifies a comma-separated list of mount directories, where `yb-master` will add a `yb-data/master` data directory, `master.err`, `master.out`, and `pg_data` directory.

Required.

Changing the value of this flag after the cluster has already been created is not supported.

##### --fs_wal_dirs

Specifies a comma-separated list of directories, where `yb-master` will store write-ahead (WAL) logs. This can be the same as one of the directories listed in `--fs_data_dirs`, but not a subdirectory of a data directory.

Default: Same value as `--fs_data_dirs`

##### --rpc_bind_addresses

Specifies the comma-separated list of the network interface addresses to which to bind for RPC connections.

The values used must match on all `yb-master` and [`yb-tserver`](../yb-tserver/#rpc-bind-addresses) configurations.

Default: Private IP address of the host on which the server is running, as defined in `/home/yugabyte/master/conf/server.conf`. For example:

```sh
egrep -i rpc /home/yugabyte/master/conf/server.conf
--rpc_bind_addresses=172.161.x.x:7100
```

Make sure that the [`server_broadcast_addresses`](#server-broadcast-addresses) flag is set correctly if the following applies:

- `rpc_bind_addresses` is set to `0.0.0.0`
- `rpc_bind_addresses` involves public IP addresses such as, for example, `0.0.0.0:7100`, which instructs the server to listen on all available network interfaces.

##### --server_broadcast_addresses

Specifies the public IP or DNS hostname of the server (with an optional port). This value is used by servers to communicate with one another, depending on the connection policy parameter.

Default: `""`

##### --dns_cache_expiration_ms

Specifies the duration, in milliseconds, until a cached DNS resolution expires. When hostnames are used instead of IP addresses, a DNS resolver must be queried to match hostnames to IP addresses. By using a local DNS cache to temporarily store DNS lookups, DNS queries can be resolved quicker and additional queries can be avoided. This reduces latency, improves load times, and reduces bandwidth and CPU consumption.

Default: `60000` (1 minute)

{{< note title="Note" >}}

If this value is changed from the default, make sure to add the same value to all YB-Master and YB-TSever configurations.

{{< /note >}}

##### --use_private_ip

Specifies the policy that determines when to use private IP addresses for inter-node communication. Possible values are `never`, `zone`, `cloud`, and `region`. Based on the values of the [geo-distribution flags](#geo-distribution-flags).

Valid values for the policy are:

- `never` — Always use the [`--server_broadcast_addresses`](#server-broadcast-addresses).
- `zone` — Use the private IP inside a zone; use the [`--server_broadcast_addresses`](#server-broadcast-addresses) outside the zone.
- `region` — Use the private IP address across all zone in a region; use [`--server_broadcast_addresses`](#server-broadcast-addresses) outside the region.

Default: `never`

##### --webserver_interface

Specifies the bind address for web server user interface access.

Default: `0.0.0.0`

##### --webserver_port

Specifies the web server monitoring port.

Default: `7000`

##### --webserver_doc_root

Monitoring web server home.

Default: The `www` directory in the YugabyteDB home directory.

##### --webserver_certificate_file

Location of the SSL certificate file (in .pem format) to use for the web server. If empty, SSL is not enabled for the web server.

Default: `""`

##### --webserver_authentication_domain

Domain used for .htpasswd authentication. This should be used in conjunction with [`--webserver_password_file`](#webserver-password-file).

Default: `""`

##### --webserver_password_file

Location of .htpasswd file containing usernames and hashed passwords, for authentication to the web server.

Default: `""`

##### --defer_index_backfill

If enabled, yb-master avoids launching any new index-backfill jobs on the cluster for all new YCQL indexes.
You will need to run [`yb-admin backfill_indexes_for_table`](../../../admin/yb-admin/#backfill-indexes-for-table) manually for indexes to be functional.
See [`CREATE DEFERRED INDEX`](../../../api/ycql/ddl_create_index/#deferred-index) for reference.

Default: `false`

##### --allow_batching_non_deferred_indexes

If enabled, indexes on the same (YCQL) table may be batched together during backfill, even if they were not deferred.

Default: `true`

## YSQL flags

##### --enable_ysql

{{< note title="Note" >}}

Ensure that `enable_ysql` values in `yb-master` configurations match the values in `yb-tserver` configurations.

{{< /note >}}

Enables the YSQL API when value is `true`.

Default: `true`

## Logging flags

##### --colorlogtostderr

Color messages logged to `stderr` (if supported by terminal).

Default: `false`

##### --logbuflevel

Buffer log messages logged at this level (or lower).

Valid values: `-1` (don't buffer); `0` (INFO); `1` (WARN); `2` (ERROR); `3` (FATAL)

Default: `0`

##### --logbufsecs

Buffer log messages for at most this many seconds.

Default: `30`

##### --logtostderr

Write log messages to `stderr` instead of `logfiles`.

Default: `false`

##### --log_dir

The directory to write `yb-master` log files.

Default: Same as [`--fs_data_dirs`](#fs-data-dirs)

##### --log_link

Put additional links to the log files in this directory.

Default: `""`

##### --log_prefix

Prepend the log prefix to each log line.

Default:  `true`

##### --max_log_size

The maximum log size, in megabytes (MB). A value of `0` will be silently overridden to `1`.

Default: `1800` (1.8 GB)

##### --minloglevel

The minimum level to log messages. Values are: `0` (INFO), `1` (WARN), `2` (ERROR), `3` (FATAL).

Default: `0` (INFO)

##### --stderrthreshold

Log messages at, or above, this level are copied to `stderr` in addition to log files.

Default: `2`

## Raft flags

For a typical deployment, values used for Raft and the write ahead log (WAL) flags in `yb-master` configurations should match the values in [yb-tserver](../yb-tserver/#raft-flags) configurations.

##### --follower_unavailable_considered_failed_sec

The duration, in seconds, after which a follower is considered to be failed because the leader has not received a heartbeat.

Default: `7200` (2 hours)

The `--follower_unavailable_considered_failed_sec` value should match the value for [`--log_min_seconds_to_retain`](#log-min-seconds-to-retain).

##### --evict_failed_followers

Failed followers will be evicted from the Raft group and the data will be re-replicated.

Default: `false`

Note that it is not recommended to set the flag to true for masters as you cannot automatically recover a failed master once it is evicted.

##### --leader_failure_max_missed_heartbeat_periods

The maximum heartbeat periods that the leader can fail to heartbeat in before the leader is considered to be failed. The total failure timeout, in milliseconds, is [`--raft_heartbeat_interval_ms`](#raft-heartbeat-interval-ms) multiplied by `--leader_failure_max_missed_heartbeat_periods`.

For read replica clusters, set the value to `10` in all `yb-tserver` and `yb-master` configurations.  Because the data is globally replicated, RPC latencies are higher. Use this flag to increase the failure detection interval in such a higher RPC latency deployment.

Default: `6`

##### --leader_lease_duration_ms

The leader lease duration, in milliseconds. A leader keeps establishing a new lease or extending the existing one with every consensus update. A new server is not allowed to serve as a leader (that is, serve up-to-date read requests or acknowledge write requests) until a lease of this duration has definitely expired on the old leader's side, or the old leader has explicitly acknowledged the new leader's lease.

This lease allows the leader to safely serve reads for the duration of its lease, even during a network partition. For more information, refer to [Leader leases](../../../architecture/transactions/single-row-transactions/#leader-leases-reading-the-latest-data-in-case-of-a-network-partition).

Leader lease duration should be longer than the heartbeat interval, and less than the multiple of `--leader_failure_max_missed_heartbeat_periods` multiplied by `--raft_heartbeat_interval_ms`.

Default: `2000`

##### --raft_heartbeat_interval_ms

The heartbeat interval, in milliseconds, for Raft replication. The leader produces heartbeats to followers at this interval. The followers expect a heartbeat at this interval and consider a leader to have failed if it misses several in a row.

Default: `500`

### Write ahead log (WAL) flags

Ensure that values used for the write ahead log (WAL) in `yb-master` configurations match the values in `yb-tserver` configurations.

##### --fs_wal_dirs

The directory where the `yb-tserver` retains WAL files. May be the same as one of the directories listed in [`--fs_data_dirs`](#fs-data-dirs), but not a subdirectory of a data directory.

Default: Same as `--fs_data_dirs`

##### --durable_wal_write

If set to `false`, the writes to the WAL are synced to disk every [`interval_durable_wal_write_ms`](#interval-durable-wal-write-ms) milliseconds (ms) or every [`bytes_durable_wal_write_mb`](#bytes-durable-wal-write-mb) megabyte (MB), whichever comes first. This default setting is recommended only for multi-AZ or multi-region deployments where the availability zones (AZs) or regions are independent failure domains and there is not a risk of correlated power loss. For single AZ deployments, this flag should be set to `true`.

Default: `false`

##### --interval_durable_wal_write_ms

When [`--durable_wal_write`](#durable-wal-write) is false, writes to the WAL are synced to disk every `--interval_durable_wal_write_ms` or [`--bytes_durable_wal_write_mb`](#bytes-durable-wal-write-mb), whichever comes first.

Default: `1000`

##### --bytes_durable_wal_write_mb

When [`--durable_wal_write`](#durable-wal-write) is `false`, writes to the WAL are synced to disk every `--bytes_durable_wal_write_mb` or `--interval_durable_wal_write_ms`, whichever comes first.

Default: `1`

##### --log_min_seconds_to_retain

The minimum duration, in seconds, to retain WAL segments, regardless of durability requirements. WAL segments can be retained for a longer amount of time, if they are necessary for correct restart. This value should be set long enough such that a tablet server which has temporarily failed can be restarted in the given time period.

Default: `7200` (2 hours)

The `--log_min_seconds_to_retain` value should match the value for [`--follower_unavailable_considered_failed_sec`](#follower-unavailable-considered-failed-sec).

##### --log_min_segments_to_retain

The minimum number of WAL segments (files) to retain, regardless of durability requirements. The value must be at least `1`.

Default: `2`

##### --log_segment_size_mb

The size, in megabytes (MB), of a WAL segment (file). When the WAL segment reaches the specified size, then a log rollover occurs and a new WAL segment file is created.

Default: `64`

##### --reuse_unclosed_segment_threshold_bytes

When the server restarts from a previous crash, if the tablet's last WAL file size is less than or equal to this threshold value, the last WAL file will be reused. Otherwise, WAL will allocate a new file at bootstrap. To disable WAL reuse, set the value to `-1`.

Default: The default value in `2.18.1` is `-1` - feature is disabled by default. The default value starting from `2.19.1` is `524288` (0.5 MB) - feature is enabled by default.

## Load balancing flags

For information on YB-Master load balancing, see [Data placement and load balancing](../../../architecture/concepts/yb-master/#data-placement-and-load-balancing).

For load balancing commands in `yb-admin`, see [Rebalancing commands (yb-admin)](../../../admin/yb-admin/#rebalancing-commands).

##### --enable_load_balancing

Enables or disables the load balancing algorithm, to move tablets around.

Default: `true`

##### --leader_balance_threshold

Specifies the number of leaders per tablet server to balance below. If this is configured to `0` (the default), the leaders will be balanced optimally at extra cost.

Default: `0`

##### --leader_balance_unresponsive_timeout_ms

Specifies the period of time, in milliseconds, that a YB-Master can go without receiving a heartbeat from a YB-TServer before considering it unresponsive. Unresponsive servers are excluded from leader balancing.

Default: `3000` (3 seconds)

##### --load_balancer_max_concurrent_adds

Specifies the maximum number of tablet peer replicas to add in a load balancer operations.

Default: `1`

##### --load_balancer_max_concurrent_moves

Specifies the maximum number of tablet leaders on tablet servers (across the cluster) to move in a load balancer operation.

Default: `2`

##### --load_balancer_max_concurrent_moves_per_table

Specifies the maximum number of tablet leaders per table to move in any one run of the load balancer. The maximum number of tablet leader moves across the cluster is still limited by the flag `load_balancer_max_concurrent_moves`. This flag is meant to prevent a single table from using all of the leader moves quota and starving other tables.

Default: `1`

##### --load_balancer_max_concurrent_removals

Specifies the maximum number of over-replicated tablet peer removals to do in a load balancer operation.

Default: `1`

##### --load_balancer_max_concurrent_tablet_remote_bootstraps

Specifies the maximum number of tablets being remote bootstrapped across the cluster.

Default: `10`

##### --load_balancer_max_concurrent_tablet_remote_bootstraps_per_table

Maximum number of tablets being remote bootstrapped for any table. The maximum number of remote bootstraps across the cluster is still limited by the flag `load_balancer_max_concurrent_tablet_remote_bootstraps`. This flag is meant to prevent a single table use all the available remote bootstrap sessions and starving other tables.

Default: `2`

##### --load_balancer_max_over_replicated_tablets

Specifies the maximum number of running tablet replicas that are allowed to be over the configured replication factor.

Default: `1`

##### --load_balancer_num_idle_runs

Specifies the number of idle runs of load balancer to deem it idle.

Default: `5`

##### --load_balancer_skip_leader_as_remove_victim

Should the LB skip a leader as a possible remove candidate.

Default: `false`

## Sharding flags

##### --max_clock_skew_usec

The expected maximum clock skew, in microseconds (µs), between any two servers in your deployment.

Default: `500000` (500,000 µs = 500ms)

##### --replication_factor

The number of replicas, or copies of data, to store for each tablet in the universe.

Default: `3`

##### --yb_num_shards_per_tserver

The number of shards (tablets) per YB-TServer for each YCQL table when a user table is created.

Default: `-1`, where the number of shards is determined at runtime, as follows:

- If [enable_automatic_tablet_splitting](#enable-automatic-tablet-splitting) is `true`
  - The default value is considered as `1`.
  - For servers with 4 CPU cores or less, the number of tablets for each table doesn't depend on the number of YB-TServers. Instead, for 2 CPU cores or less, 1 tablet per cluster is created; for 4 CPU cores or less, 2 tablets per cluster are created.

- If `enable_automatic_tablet_splitting` is `false`
  - For servers with up to two CPU cores, the default value is considered as `4`.
  - For three or more CPU cores, the default value is considered as `8`.

Local cluster installations created using `yb-ctl` and `yb-docker-ctl` use a default value of `2` for this flag.

Clusters created using `yugabyted` always use a default value of `1`.

{{< note title="Note" >}}

- This value must match on all `yb-master` and `yb-tserver` configurations of a YugabyteDB cluster.
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

Local cluster installations created using `yb-ctl` and `yb-docker-ctl` use a default value of `2` for this flag.

Clusters created using `yugabyted` always use a default value of `1`.

{{< note title="Note" >}}

- This value must match on all `yb-master` and `yb-tserver` configurations of a YugabyteDB cluster.
- If the value is set to *Default* (`-1`), the system automatically determines an appropriate value based on the number of CPU cores and internally *updates* the flag with the intended value during startup prior to version 2.18 and the flag remains *unchanged* starting from version 2.18.
- The [`CREATE TABLE ...SPLIT INTO`](../../../api/ysql/the-sql-language/statements/ddl_create_table/#split-into) clause can be used on a per-table basis to override the `ysql_num_shards_per_tserver` value.

{{< /note >}}

##### --ysql_colocate_database_by_default

When enabled, all databases created in the cluster are colocated by default. If you enable the flag after creating a cluster, you need to restart the YB-Master and YB-TServer services.

For more details, see [clusters in colocated tables](../../../architecture/docdb-sharding/colocated-tables/#clusters).

Default: `false`

## Tablet splitting flags

##### --max_create_tablets_per_ts

The maximum number of tablets per tablet server that can be specified when creating a table. This also limits the number of tablets that can be created by tablet splitting.

Default: `50`

##### --enable_automatic_tablet_splitting

Enables YugabyteDB to [automatically split tablets](../../../architecture/docdb-sharding/tablet-splitting/#automatic-tablet-splitting), based on the specified tablet threshold sizes configured below.

Default: `true`

{{< note title="Important" >}}

This value must match on all `yb-master` and `yb-tserver` configurations of a YugabyteDB cluster.

{{< /note >}}

##### --tablet_split_low_phase_shard_count_per_node

The threshold number of shards (per cluster node) in a table below which automatic tablet splitting will use [`--tablet_split_low_phase_size_threshold_bytes`](./#tablet-split-low-phase-size-threshold-bytes) to determine which tablets to split.

Default: `1`

##### --tablet_split_low_phase_size_threshold_bytes

The size threshold used to determine if a tablet should be split when the tablet's table is in the "low" phase of automatic tablet splitting. See [`--tablet_split_low_phase_shard_count_per_node`](./#tablet-split-low-phase-shard-count-per-node).

Default: `128 MiB`

##### --tablet_split_high_phase_shard_count_per_node

The threshold number of shards (per cluster node) in a table below which automatic tablet splitting will use [`--tablet_split_high_phase_size_threshold_bytes`](./#tablet-split-low-phase-size-threshold-bytes) to determine which tablets to split.

Default: `24`

##### --tablet_split_high_phase_size_threshold_bytes

The size threshold used to determine if a tablet should be split when the tablet's table is in the "high" phase of automatic tablet splitting. See [`--tablet_split_high_phase_shard_count_per_node`](./#tablet-split-low-phase-shard-count-per-node).

Default: `10 GiB`

##### --tablet_force_split_threshold_bytes

The size threshold used to determine if a tablet should be split even if the table's number of shards puts it past the "high phase".

Default: `100 GiB`

##### --tablet_split_limit_per_table

The maximum number of tablets per table for tablet splitting. Limitation is disabled if this value is set to 0.

Default: `0`

##### --index_backfill_tablet_split_completion_timeout_sec

Total time to wait for tablet splitting to complete on a table on which a backfill is running before aborting the backfill and marking it as failed.

Default: `30`

##### --index_backfill_tablet_split_completion_poll_freq_ms

Delay before retrying to see if tablet splitting has completed on the table on which a backfill is running.

Default: `2000`

##### --process_split_tablet_candidates_interval_msec

The minimum time between automatic splitting attempts. The actual splitting time between runs is also affected by `catalog_manager_bg_task_wait_ms`, which controls how long the background tasks thread sleeps at the end of each loop.

Default: `0`

##### --outstanding_tablet_split_limit

Limits the number of total outstanding tablet splits. Limitation is disabled if value is set to `0`. Limit includes tablets that are performing post-split compactions.

Default: `0`

##### --outstanding_tablet_split_limit_per_tserver

Limits the number of outstanding tablet splits per node. Limitation is disabled if value is set to `0`. Limit includes tablets that are performing post-split compactions.

Default: `1`

##### --enable_tablet_split_of_pitr_tables

Enables automatic tablet splitting of tables covered by Point In Time Recovery schedules.

Default: `true`

##### --enable_tablet_split_of_xcluster_replicated_tables

Enables automatic tablet splitting for tables that are part of an xCluster replication setup.

Default: `false`

To enable tablet splitting on cross cluster replicated tables, this flag should be set to `true` on both the producer and consumer clusters, as they will perform splits independently of each other. Both the producer and consumer clusters must be running v2.14.0+ to enable the feature (relevant in case of cluster upgrades).

##### --prevent_split_for_ttl_tables_for_seconds

Number of seconds between checks for whether to split a tablet with a default TTL. Checks are disabled if this value is set to 0.

Default: `86400`

##### --prevent_split_for_small_key_range_tablets_for_seconds

Number of seconds between checks for whether to split a tablet whose key range is too small to be split. Checks are disabled if this value is set to 0.

Default: `300`

##### --sort_automatic_tablet_splitting_candidates

Determines whether to sort automatic split candidates from largest to smallest (prioritizing larger tablets for split).

Default: `true`

Syntax:

```sh
yb-admin --master_addresses <master-addresses> --tablet_force_split_size_threshold_bytes <bytes>
```

- *master-addresses*: Comma-separated list of YB-Master hosts and ports. Default value is `localhost:7100`.
- *bytes*: The threshold size, in bytes, after which tablets should be split. Default value of `0` disables automatic tablet splitting.

For details on automatic tablet splitting, see the following:

- [Automatic tablet splitting](../../../architecture/docdb-sharding/tablet-splitting) — Architecture overview.
- [Automatic Re-sharding of Data with Tablet Splitting](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/docdb-automatic-tablet-splitting.md) — Architecture design document in the GitHub repository.

## Geo-distribution flags

Settings related to managing geo-distributed clusters.

##### --placement_zone

The name of the availability zone (AZ), or rack, where this instance is deployed.

Default: `rack1`

##### --placement_region

Name of the region or data center where this instance is deployed.

Default: `datacenter1`

##### --placement_cloud

Name of the cloud where this instance is deployed.

Default: `cloud1`

##### --placement_uuid

The unique identifier for the cluster.

Default: `""`

##### -- use_private_ip

Determines when to use private IP addresses. Possible values are `never` (default),`zone`,`cloud` and `region`. Based on the values of the `placement_*` configuration flags.

Default: `never`

##### --auto_create_local_transaction_tables

If true, transaction tables will be automatically created for any YSQL tablespace which has a placement and at least one other table in it.

Default: `true`

## Security flags

For details on enabling encryption in transit, see [Encryption in transit](../../../secure/tls-encryption/).

##### --certs_dir

Directory that contains certificate authority, private key, and certificates for this server.

Default: `""` (uses `<data drive>/yb-data/master/data/certs`.)

##### --certs_for_client_dir

The directory that contains certificate authority, private key, and certificates for this server that should be used for client-to-server communications.

Default: `""` (Use the same directory as certs_dir.)

##### --allow_insecure_connections

Allow insecure connections. Set to `false` to prevent any process with unencrypted communication from joining a cluster. Note that this flag requires [use_node_to_node_encryption](#use-node-to-node-encryption) to be enabled and [use_client_to_server_encryption](#use-client-to-server-encryption) to be enabled.

Default: `true`

##### --dump_certificate_entries

Adds certificate entries, including IP addresses and hostnames, to log for handshake error messages. Enabling this flag is helpful for debugging certificate issues.

Default: `false`

##### --use_client_to_server_encryption

Use client-to-server (client-to-node) encryption to protect data in transit between YugabyteDB servers and clients, tools, and APIs.

Default: `false`

##### --use_node_to_node_encryption

Enables server-server (node-to-node) encryption between YB-Master and YB-TServer servers in a cluster or universe. To work properly, all YB-TServer servers must also have their [--use_node_to_node_encryption](../yb-tserver/#use-node-to-node-encryption) flag enabled.

When enabled, [--allow_insecure_connections](#allow-insecure-connections) should be set to false to disallow insecure connections.

Default: `false`

##### --cipher_list

Specify cipher lists for TLS 1.2 and earlier versions. (For TLS 1.3, use [--ciphersuite](#ciphersuite).) Use a colon-separated list of TLS 1.2 cipher names in order of preference. Use an exclamation mark ( `!` ) to exclude ciphers. For example:

```sh
--cipher_list DEFAULTS:!DES:!IDEA:!3DES:!RC2
```

This allows all ciphers for TLS 1.2 to be accepted, except those matching the category of ciphers omitted.

This flag requires a restart or rolling restart.

Default: `DEFAULTS`

For more information, refer to [SSL_CTX_set_cipher_list](https://www.openssl.org/docs/man1.1.1/man3/SSL_CTX_set_cipher_list.html) in the OpenSSL documentation.

##### --ciphersuite

Specify cipher lists for TLS 1.3. For TLS 1.2 and earlier, use [--cipher_list](#cipher-list).

Use a colon-separated list of TLS 1.3 ciphersuite names in order of preference. Use an exclamation mark ( ! ) to exclude ciphers. For example:

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

In addition, as this setting does not propagate to PostgreSQL, it is recommended that you specify the minimum TLS version (`ssl_min_protocol_version`) for PostgreSQL by setting the [ysql_pg_conf_csv](#ysql-pg-conf-csv) flag as follows:

```sh
--ysql_pg_conf_csv="ssl_min_protocol_version='TLSv1.2'"
```

## Change data capture (CDC) flags

To learn about CDC, see [Change data capture (CDC)](../../../architecture/docdb-replication/change-data-capture/).

For information on other CDC configuration flags, see [YB-TServer's CDC flags](../yb-tserver/#change-data-capture-cdc-flags).

##### --cdc_state_table_num_tablets

The number of tablets to use when creating the CDC state table. Used in both xCluster and CDCSDK.

Default: `0` (Use the same default number of tablets as for regular tables.)

##### --cdc_wal_retention_time_secs

WAL retention time, in seconds, to be used for tables for which a CDC stream was created. Used in both xCluster and CDCSDK.

Default: `14400` (4 hours)

##### --enable_truncate_cdcsdk_table

By default, TRUNCATE commands on tables on which CDCSDK stream is active will fail. Changing the value of this flag from `false` to `true` will enable truncating the tables part of the CDCSDK stream.

Default: `false`

## Metric export flags

##### --export_help_and_type_in_prometheus_metrics

YB-Master metrics are available in Prometheus format at
`http://localhost:7000/prometheus-metrics`.  This flag controls whether
#TYPE and #HELP information is included as part of the Prometheus
metrics output by default.

To override this flag on a per-scrape basis, set the URL parameter
`show_help` to `true` to include or to `false` to not include type and
help information.  For example, querying
`http://localhost:7000/prometheus-metrics?show_help=true` will return
type and help information regardless of the setting of this flag.

Default: `true`

## Advanced flags

##### ysql_index_backfill_rpc_timeout_ms

Deadline (in milliseconds) for each internal YB-Master to YB-TServer RPC for backfilling a chunk of the index.

Default: 1 minute

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
