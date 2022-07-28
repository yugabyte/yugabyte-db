---
title: yb-tserver configuration reference
headerTitle: yb-tserver
linkTitle: yb-tserver
description: YugabyteDB Tablet Server (yb-tserver/) binary and configuration flags to store and manage data for client applications.
menu:
  stable:
    identifier: yb-tserver
    parent: configuration
    weight: 2440
type: docs
---

Use the `yb-tserver` binary and its flags to configure the [YB-TServer](../../../architecture/concepts/yb-tserver/) server. The `yb-tserver` executable file is located in the `bin` directory of YugabyteDB home.

## Syntax

```sh
yb-tserver [ flags ]
```

### Example

```sh
$ ./bin/yb-tserver \
--tserver_master_addrs 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
--rpc_bind_addresses 172.151.17.130 \
--start_pgsql_proxy \
--fs_data_dirs "/home/centos/disk1,/home/centos/disk2" &
```

### Online help

To display the online help, run `yb-tserver --help` from the YugabyteDB home directory.

```sh
$ ./bin/yb-tserver --help
```

## Configuration flags

- [Help](#help-flags)
- [General](#general-flags)
- [Logging](#logging-flags)
- [Raft](#raft-flags)
  - [Write Ahead Log (WAL)](#write-ahead-log-wal-flags)
- [Sharding](#sharding-flags)
- [Geo-distribution](#geo-distribution-flags)
- [YSQL](#ysql-flags)
- [YCQL](#ycql-flags)
- [YEDIS](#yedis-flags)
- [Performance](#performance-flags)
- [Security](#security-flags)
- [Change data capture (CDC)](#change-data-capture-cdc-flags)

---

### Help flags

##### --help

Displays help on all flags.

##### --helpon

Displays help on modules named by the specified flag value.

---

### General flags

##### --flagfile

Specifies the file to load the configuration flags from. The configuration flags must be in the same format as supported by the command line flags.

##### --version

Shows version and build info, then exits.

##### --tserver_master_addrs

Specifies a comma-separated list of all the `yb-master` RPC addresses.

Required.

Default: `127.0.0.1:7100`

{{< note title="Note" >}}

The number of comma-separated values should match the total number of YB-Master servers (or the replication factor).

{{< /note >}}

##### --fs_data_dirs

Specifies a comma-separated list of mount directories, where `yb-tserver` will add a `yb-data/tserver` data directory, `tserver.err`, `tserver.out`, and `pg_data` directory.

Required.

Changing the value of this flag after the cluster has already been created is not supported.

##### --fs_wal_dirs

Specifies a comma-separated list of directories, where `yb-tserver` will store write-ahead (WAL) logs. This can be the same as one of the directories listed in `--fs_data_dirs`, but not a subdirectory of a data directory.

Default: Same value as `--fs_data_dirs`

##### --max_clock_skew_usec

Specifies the expected maximum clock skew, in microseconds (µs), between any two nodes in your deployment.

Default: `500000` (500,000 µs = 500ms)

##### --rpc_bind_addresses

Specifies the comma-separated list of the network interface addresses to bind to for RPC connections:

- Typically, the value is set to the private IP address of the host on which the server is running. When using the default, or explicitly setting the value to `0.0.0.0:9100`, the server will listen on all available network interfaces.

- The values must match on all [`yb-master`](../yb-master/#rpc-bind-addresses) and `yb-tserver` configurations.

Default: `0.0.0.0:9100`

{{< note title="Note" >}}

In cases where `rpc_bind_addresses` is set to `0.0.0.0` (or not explicitly set, and uses the default) or in cases involving public IP addresses, make sure that [`server_broadcast_addresses`](#server-broadcast-addresses) is correctly set.

{{< /note >}}

##### --server_broadcast_addresses

Specifies the public IP or DNS hostname of the server (with an optional port). This value is used by servers to communicate with one another, depending on the connection policy parameter.

Default: `""`

##### --dns_cache_expiration_ms

Specifies the duration, in milliseconds, until a cached DNS resolution expires. When hostnames are used instead of IP addresses, a DNS resolver must be queried to match hostnames to IP addresses. By using a local DNS cache to temporarily store DNS lookups, DNS queries can be resolved quicker and additional queries can be avoided, thereby reducing latency, improving load times, and reducing bandwidth and CPU consumption.

Default: `60000` (1 minute)

{{< note title="Note" >}}

If this value is changed from the default, make sure to add the identical value to all YB-Master and YB-TSever configurations.

{{< /note >}}

##### --use_private_ip

Specifies the policy that determines when to use private IP addresses for inter-node communication. Possible values are `never` (default),`zone`,`cloud` and `region`. Based on the values of the [placement (`--placement_*`) configuration flags](#placement-flags).

Valid values for the policy are:

- `never` — Always use the [`--server_broadcast_addresses`](#server-broadcast-addresses).
- `zone` — Use the private IP inside a zone; use the [`--server_broadcast_addresses`](#server-broadcast-addresses) outside the zone.
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

Domain used for .htpasswd authentication. This should be used in conjunction with [`--webserver_password_file`](#webserver-password-file).

Default: `""`

##### --webserver_password_file

Location of .htpasswd file containing usernames and hashed passwords, for authentication to the web server.

Default: `""`

---

### Logging flags

##### --log_dir

The directory to write `yb-tserver` log files.

Default: Same as [`--fs_data_dirs`](#fs-data-dirs)

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

---

### Raft flags

{{< note title="Note" >}}

Ensure that values used for Raft and the write ahead log (WAL) in `yb-tserver` configurations match the values in `yb-master` configurations.

{{< /note >}}

##### --follower_unavailable_considered_failed_sec

The duration, in seconds, after which a follower is considered to be failed because the leader has not received a heartbeat. The follower is then evicted from the configuration and the data is rereplicated elsewhere.

Default: `900` (15 minutes)

{{< note title="Important" >}}

The `--follower_unavailable_considered_failed_sec` value should match the value for [`--log_min_seconds_to_retain`](#log-min-seconds-to-retain).

{{< /note >}}

##### --leader_failure_max_missed_heartbeat_periods

The maximum heartbeat periods that the leader can fail to heartbeat in before the leader is considered to be failed. The total failure timeout, in milliseconds (ms), is [`--raft_heartbeat_interval_ms`](#raft-heartbeat-interval-ms) multiplied by `--leader_failure_max_missed_heartbeat_periods`.

For read replica clusters, set the value to `10` in all `yb-tserver` and `yb-master` configurations.  Because the data is globally replicated, RPC latencies are higher. Use this flag to increase the failure detection interval in such a higher RPC latency deployment.

Default: `6`

##### --max_stale_read_bound_time_ms

Specifies the maximum bounded staleness (duration), in milliseconds, before a follower forwards a read request to the leader.
In a geo-distributed cluster, with followers located a long distance from the tablet leader, you can use this setting to increase the maximum bounded staleness.

Default: `10000` (10 seconds)

##### --raft_heartbeat_interval_ms

The heartbeat interval, in milliseconds (ms), for Raft replication. The leader produces heartbeats to followers at this interval. The followers expect a heartbeat at this interval and consider a leader to have failed if it misses several in a row.

Default: `500`

#### Write ahead log (WAL) flags

{{< note title="Note" >}}

Ensure that values used for the write ahead log (WAL) in `yb-tserver` configurations match the values for `yb-master` configurations.

{{< /note >}}

##### --fs_wal_dirs

The directory where the `yb-tserver` retains WAL files. May be the same as one of the directories listed in [`--fs_data_dirs`](#fs-data-dirs), but not a subdirectory of a data directory.

Default: Same as `--fs_data_dirs`

##### --durable_wal_write

If set to `false`, the writes to the WAL are synced to disk every [`interval_durable_wal_write_ms`](#interval-durable-wal-write-mas) milliseconds (ms) or every [`bytes_durable_wal_write_mb`](#bytes-durable-wal-write-mb) megabyte (MB), whichever comes first. This default setting is recommended only for multi-AZ or multi-region deployments where the availability zones (AZs) or regions are independent failure domains and there is not a risk of correlated power loss. For single AZ deployments, this flag should be set to `true`.

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

---

### Sharding flags

##### --yb_num_shards_per_tserver

The number of shards per YB-TServer for each YCQL table when a user table is created.

Default: `-1` (server internally sets default value). For servers with up to two CPU cores, the default value is `4`. For three or more CPU cores, the default value is `8`. Local cluster installations, created with `yb-ctl` and `yb-docker-ctl`, use a value of `2` for this flag. Clusters created with `yugabyted` use a default value of `1`.

{{< note title="Important" >}}

This value must match on all `yb-master` and `yb-tserver` configurations of a YugabyteDB cluster.

{{< /note >}}

{{< note title="Note" >}}

On a per-table basis, the [`CREATE TABLE ... WITH TABLETS = <num>`](../../../api/ycql/ddl_create_table/#create-a-table-specifying-the-number-of-tablets) clause can be used to override the `yb_num_shards_per_tserver` value.

{{< /note >}}

##### --ysql_num_shards_per_tserver

The number of shards per YB-TServer for each YSQL table when a user table is created.

Default: `-1` (server internally sets default value). For servers with up to two CPU cores, the default value is `2`. For servers with three or four CPU cores, the default value is `4`. Beyond four cores, the default value is `8`. Local cluster installations, created with `yb-ctl` and `yb-docker-ctl`, use a value of `2` for this flag. Clusters created with `yugabyted` use a default value of `1`.

{{< note title="Important" >}}

This value must match on all `yb-master` and `yb-tserver` configurations of a YugabyteDB cluster.

{{< /note >}}

{{< note title="Note" >}}

On a per-table basis, the [`CREATE TABLE ...SPLIT INTO`](../../../api/ysql/the-sql-language/statements/ddl_create_table/#split-into) clause can be used to override the `ysql_num_shards_per_tserver` value.

{{< /note >}}

---

### Geo-distribution flags

Settings related to managing geo-distributed clusters.

##### --placement_zone

The name of the availability zone, or rack, where this instance is deployed.

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

If true, forces all transactions through this instance to always be global transactions that use the `system.transactions` transaction status table. This is equivalent to always setting the session variable `force_global_transaction = TRUE`.

{{< note title="Global transaction latency" >}}

Avoid setting this flag when possible. All distributed transactions _can_ run without issue as global transactions, but you may have significantly higher latency when committing transactions, because YugabyteDB must achieve consensus across multiple regions to write to `system.transactions`. When necessary, it is preferable to selectively set the session variable `force_global_transaction = TRUE` rather than setting this flag.

{{< /note >}}

Default: `false`

#### --auto-create-local-transaction-tables

If true, transaction status tables will be created under each YSQL tablespace that has a placement set and contains at least one other table.

Default: `true`

#### --auto-promote-nonlocal-transactions-to-global

If true, local transactions using transaction status tables other than `system.transactions` will be automatically promoted to global transactions using the `system.transactions` transaction status table upon accessing data outside of the local region.

Default: `true`

---

### YSQL flags

The following flags support the use of the [YSQL API](../../../api/ysql/).

##### --enable_ysql

{{< note title="Note" >}}

Ensure that `enable_ysql` in `yb-tserver` configurations match the values in `yb-master` configurations.

{{< /note >}}

Enables the YSQL API. Replaces the deprecated `--start_pgsql_proxy` flag.

Default: `true`

##### --ysql_enable_auth

Enables YSQL authentication.

{{< note title="Note" >}}

**Release 2.0:** Assign a password for the default `yugabyte` user to be able to sign in after enabling YSQL authentication.

**Release 2.0.1:** When YSQL authentication is enabled, you can sign into `ysqlsh` using the default `yugabyte` user that has a default password of `yugabyte`.

{{< /note >}}

Default: `false`

##### --pgsql_proxy_bind_address

Specifies the TCP/IP bind addresses for the YSQL API. The default value of `0.0.0.0:5433` allows listening for all IPv4 addresses access to localhost on port `5433`. The `--pgsql_proxy_bind_address` value overwrites `listen_addresses` (default value of `127.0.0.1:5433`) that controls which interfaces accept connection attempts.

To specify fine-grained access control over who can access the server, use [`--ysql_hba_conf`](#ysql-hba-conf).

Default: `0.0.0.0:5433`

##### --pgsql_proxy_webserver_port

Specifies the web server port for YSQL metrics monitoring.

Default: `13000`

##### --ysql_hba_conf

{{< note title="Note" >}}
`--ysql_hba_conf` tserver flag is deprecated. Use `--ysql_hba_conf_csv` instead.
{{< /note >}}

Specifies a comma-separated list of PostgreSQL client authentication settings that is written to the `ysql_hba.conf` file.

Default: `"host all all 0.0.0.0/0 trust,host all all ::0/0 trust"`

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

##### --ysql_pg_conf_csv

Comma-separated list of PostgreSQL server configuration parameters that is appended to the `postgresql.conf` file.

For example:

```sh
--ysql_pg_conf_csv="suppress_nonpg_logs=true"
```

For information on available PostgreSQL server configuration parameters, refer to [Server Configuration](https://www.postgresql.org/docs/11/runtime-config.html) in the PostgreSQL documentation.

The server configuration parameters for YugabyteDB are the same as for PostgreSQL, with the exception of some logging options. Refer to [PostgreSQL logging options](#postgresql-logging-options).

##### --ysql_timezone

Specifies the time zone for displaying and interpreting timestamps.

Default: Uses the YSQL time zone.

##### --ysql_datestyle

Specifies the display format for data and time values.

Default: Uses the YSQL display format.

##### --ysql_max_connections

Specifies the maximum number of concurrent YSQL connections.

Default: 300 for superusers. Non-superuser roles see only the connections available for use, while superusers see all connections, including those reserved for superusers.

##### --ysql_default_transaction_isolation

Specifies the default transaction isolation level.

Valid values: `SERIALIZABLE`, `REPEATABLE READ`, `READ COMMITTED`, and `READ UNCOMMITTED`.

Default: `READ COMMITTED`<sup>$</sup>

<sup>$</sup> Read Committed Isolation is supported only if the tserver gflag `yb_enable_read_committed_isolation` is set to `true`. By default this gflag is `false` and in this case the Read Committed isolation level of Yugabyte's transactional layer falls back to the stricter Snapshot Isolation (in which case `READ COMMITTED` and `READ UNCOMMITTED` of YSQL also in turn use Snapshot Isolation). Read Committed support is currently in [Beta](/preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag).

##### --ysql_disable_index_backfill

Set this flag to `false` to enable online index backfill. When set to `false`, online index builds run while online, without failing other concurrent writes and traffic.

For details on how online index backfill works, see the [Online Index Backfill](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md) design document.

Default: `false`

##### --ysql_sequence_cache_minval

Specify the minimum number of sequence values to cache in the client for every sequence object.

To turn off the default size of cache flag, set the flag to `0`.

For details on the expected behaviour when used with the sequence cache clause, see the semantics under [CREATE SEQUENCE](../../../api/ysql/the-sql-language/statements/ddl_create_sequence/#cache-cache) and [ALTER SEQUENCE](../../../api/ysql/the-sql-language/statements/ddl_alter_sequence/#cache-cache) pages.

Default: `100`

##### --ysql_log_statement

Specifies the types of YSQL statements that should be logged.

Valid values: `none` (off), `ddl` (only data definition queries, such as create/alter/drop), `mod` (all modifying/write statements, includes DDLs plus insert/update/delete/trunctate, etc), and `all` (all statements).

Default: `none`

##### --ysql_log_min_duration_statement

Logs the duration of each completed SQL statement that runs the specified duration (in milliseconds) or longer. Setting the value to `0` prints all statement durations. You can use this flag to help track down unoptimized (or "slow") queries.

Default: `-1` (disables logging statement durations)

##### --ysql_log_min_messages

Specifies the lowest YSQL message level to log.

---

### YCQL flags

The following flags support the use of the [YCQL API](../../../api/ycql/).

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

---

### YEDIS flags

The following flags support the use of the YEDIS API.

##### --redis_proxy_bind_address

Specifies the bind address for the YEDIS API.

Default: `0.0.0.0:6379`

##### --redis_proxy_webserver_port

Specifies the port for monitoring YEDIS metrics.

Default: `11000`

---

### Performance flags

Use the following two flags to select the SSTable compression type.

##### --enable_ondisk_compression

Enable SSTable compression at the cluster level.

Default: `true`

##### --compression_type

Change the SSTable compression type. The valid compression types are `Snappy`, `Zlib`, `LZ4`, and `NoCompression`.

Default: `Snappy`

{{< note title="Note" >}}

If you select an invalid option, the cluster will not come up.

If you change this flag, the change takes effect after you restart the cluster nodes.

{{< /note >}}

Changing this flag on an existing database is supported; a tablet can validly have SSTs with different compression types. Eventually, compaction will remove the old compression type files.

##### --regular_tablets_data_block_key_value_encoding

Key-value encoding to use for regular data blocks in RocksDB. Possible options: `shared_prefix`, `three_shared_parts`.

Default: `shared_prefix`

{{< note title="Note" >}}

Only change this flag to `three_shared_parts` after you migrate the whole cluster to the YugabyteDB version that supports it.

{{< /note >}}

##### --rocksdb_compact_flush_rate_limit_bytes_per_sec

Used to control rate of memstore flush and SSTable file compaction.

Default: `256MB`

##### --rocksdb_universal_compaction_min_merge_width

Compactions run only if there are at least `rocksdb_universal_compaction_min_merge_width` eligible files and their running total (summation of size of files considered so far) is within `rocksdb_universal_compaction_size_ratio` of the next file in consideration to be included into the same compaction.

Default: `4`

##### --rocksdb_universal_compaction_size_ratio

Compactions run only if there are at least `rocksdb_universal_compaction_min_merge_width` eligible files and their running total (summation of size of files considered so far) is within `rocksdb_universal_compaction_size_ratio` of the next file in consideration to be included into the same compaction.

Default: `20`

##### --timestamp_history_retention_interval_sec

The time interval, in seconds, to retain history/older versions of data. Point-in-time reads at a hybrid time prior to this interval might not be allowed after a compaction and return a `Snapshot too old` error. Set this to be greater than the expected maximum duration of any single transaction in your application.

Default: `900`

##### --remote_bootstrap_rate_limit_bytes_per_sec

Rate control across all tablets being remote bootstrapped from or to this process.

Default: `256MB`

---

### Network compression

Use the following two gflags to configure RPC compression.

##### --enable_stream_compression

Controls whether YugabyteDB uses RPC compression.

Default: `true`

##### --stream_compression_algo

Specifies which RPC compression algorithm to use. Requires `enable_stream_compression` to be set to true. Valid values are:

- 0: No compression (default value)
- 1: Gzip
- 2: Snappy
- 3: LZ4

In most cases, LZ4 (`--stream_compression_algo=3`) offers the best compromise of compression performance versus CPU overhead.

{{< note title="Upgrade notes" >}}

To upgrade from an older version that doesn't support RPC compression (such as 2.4), to a newer version that does (such as 2.6), you need to do the following:

1. Rolling restart to upgrade YugabyteDB to a version that supports compression.

1. Rolling restart to enable compression, on both master and tserver, by setting `enable_stream_compression=true`.

    \
    **Note** You can omit this step if the version you're upgrading to already has compression enabled by default. For the stable release series, versions from 2.6.3.0 and above (including all 2.8 releases) have `enable_stream_compression` set to true by default. For the preview release series, this is all releases beyond 2.9.0.

1. Rolling restart to set the compression algorithm to use, on both master and tserver, such as by setting `stream_compression_algo=3`.

{{< /note >}}

### Security flags

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

---

### Change data capture (CDC) flags

To learn about CDC, see [Change data capture (CDC)](../../../architecture/docdb-replication/change-data-capture/).

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

##### --cdc_min_replicated_index_considered_stale_seconds

If cdc_min_replicated_index hasn't been replicated in this amount of time, we reset its value to max int64 to avoid retaining any logs.

Default: `900`

##### --timestamp_history_retention_interval_sec

Time interval, in seconds, to retain history or older versions of data.

Default: `900`

##### --update_min_cdc_indices_interval_secs

How often to read the cdc_state table to get the minimum applied index for each tablet across all streams. This information is used to correctly keep log files that contain unapplied entries. This is also the rate at which a tablet's minimum replicated index across all streams is sent to the other peers in the configuration. If flag `enable_log_retention_by_op_idx` (default: `true`) is disabled, this flag has no effect.

Default: `60`

##### --cdc_checkpoint_opid_interval_ms

The number of seconds for which the client can go down and the intents will be retained. This basically means that if a client has not updated the checkpoint for this interval, the intents would be garbage collected.

Default: `60000`

{{< warning title="Warning" >}}

If you are using multiple streams, it is advised that you set this flag to `1800000` i.e. 30 minutes.

{{< /warning >}}

##### --log_max_seconds_to_retain

Number of seconds to retain log files. Log files older than this value will be deleted even if they contain unreplicated CDC entries. If 0, this flag will be ignored. This flag is ignored if a log segment contains entries that haven't been flushed to RocksDB.

Default: `86400`

##### --log_stop_retaining_min_disk_mb

Stop retaining logs if the space available for the logs falls below this limit, specified in megabytes. As with `log_max_seconds_to_retain`, this flag is ignored if a log segment contains unflushed entries.

Default: `102400`

##### --stream_truncate_record

Enable streaming of TRUNCATE record for a table on which CDC is active.

Default: `false`

##### --cdc_intent_retention_ms

The time period, in milliseconds, after which the intents will be cleaned up if there is no client polling for the change records.

Default: `14400000` (4 hours)

##### --enable_update_local_peer_min_index

Enable each local peer to update its own log checkpoint instead of the leader updating all peers.

Default: `false`

---

### File expiration based on TTL flags

##### --tablet_enable_ttl_file_filter

Turn on the file expiration for TTL feature.

Default: `false`

##### --rocksdb_max_file_size_for_compaction

For tables with a `default_time_to_live` table property, sets a size threshold at which files will no longer be considered for compaction. Files over this threshold will still be considered for expiration. Disabled if value is `0`.

Ideally, rocksdb_max_file_size_for_compaction needs to be chosen as a balance between expiring data at a reasonable frequency while also not creating too many SST files (as this can impact read performance). For instance, if 90 days worth of data is stored, perhaps this flag should be set to roughly the size of one day's worth of data.

Default: `0`

##### --sst_files_soft_limit

Threshold for number of SST files per tablet. When exceeded, writes to a tablet will be throttled until the number of files is reduced.

Default: `24`

##### --sst_files_hard_limit

Threshold for number of SST files per tablet. When exceeded, writes to a tablet will no longer be allowed until the number of files is reduced.

Default: `48`

##### --file_expiration_ignore_value_ttl

When set to true, ignores any value-level TTL metadata when determining file expiration. Useful in situations where some SST files are missing the necessary value-level metadata (in case of upgrade, for instance).

{{< warning title="Warning">}}
Use of this flag can potentially result in expiration of live data - use at your discretion.
{{< /warning >}}

Default: `false`

##### --file_expiration_value_ttl_overrides_table_ttl

When set to true, allows files to expire purely based on their value-level TTL expiration time (even if it is lower than the table TTL). This is useful for times where a file needs to expire earlier than its table-level TTL would allow. If no value-level TTL metadata is available, then table-level TTL will still be used.

{{< warning title="Warning">}}
Use of this flag can potentially result in expiration of live data - use at your discretion.
{{< /warning >}}

Default: `false`

## PostgreSQL logging options

YugabyteDB uses PostgreSQL server configuration parameters to apply server configuration settings to new server instances. You can modify these parameters using the [ysql_pg_conf_csv](#ysql-pg-conf-csv) flag.

For information on available PostgreSQL server configuration parameters, refer to [Server Configuration](https://www.postgresql.org/docs/11/runtime-config.html) in the PostgreSQL documentation.

The server configuration parameters for YugabyteDB are the same as for PostgreSQL, with the following exceptions.

### log_line_prefix

YugabyteDB supports the following additional options for the `log_line_prefix` parameter:

- %C = cloud name
- %R = region / data center name
- %Z = availability zone / rack name
- %U = cluster UUID
- %N = node and cluster name
- %H = current hostname

For information on using `log_line_prefix`, refer to [log_line_prefix](https://www.postgresql.org/docs/11/runtime-config-logging.html#GUC-LOG-LINE-PREFIX) in the PostgreSQL documentation.

### suppress_nonpg_logs (boolean)

When set, suppresses logging of non-PostgreSQL output to the postgresql log file in the `tserver/logs` directory.

Default: `off`

## Admin UI

The Admin UI for the YB-TServer is available at `http://localhost:9000`.

### Home

Home page of the YB-TServer (`yb-tserver`) that gives a high level overview of this specific instance.

![tserver-home](/images/admin/tserver-home.png)

### Dashboards

Here's a list of all dashboards to review the ongoing operations:

![tserver-dashboards](/images/admin/tserver-dashboards.png)

### Tablets

List of all tablets managed by this specific instance, sorted by the table name.

![tserver-tablets](/images/admin/tserver-tablets.png)

### Debug

List of all utilities available to debug the performance of this specific instance.

![tserver-debug](/images/admin/tserver-debug.png)
