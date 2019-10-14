---
title: yb-tserver
linkTitle: yb-tserver
description: yb-tserver
menu:
  latest:
    identifier: yb-tserver
    parent: admin
    weight: 2450
aliases:
  - admin/yb-tserver
isTocNested: false
showAsideToc: true
---

The [YB-TServer](../../architecture/concepts/universe/#yb-tserver) binary (`yb-tserver`) is located in the `bin` directory of YugabyteDB home.

## Example

```sh
$ ./bin/yb-tserver \
--tserver_master_addrs 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
--rpc_bind_addresses 172.151.17.130 \
--start_pgsql_proxy \
--fs_data_dirs "/home/centos/disk1,/home/centos/disk2" &
```

## Online help

Run `yb-tserver --help` to display the online help.

```sh
$ ./bin/yb-tserver --help
```

## Syntax

```sh
yb-tserver [ options ]
```

## Configuration options

### General options

#### --help

Displays help on all options.

#### --helpon

Displays help on modules named by the specified option (or flag) value.

#### --flagfile

Specifies the file to load the configuration options (or flags) from. The configuration settings, or flags, must be in the same format as supported by the command line options.

#### --version

Shows version and build info, then exits.

#### --tserver_master_addrs

Comma-separated list of all the `yb-master` RPC addresses. Mandatory.

#### --fs_data_dirs

Comma-separated list of directories where the `yb-tserver` will place it's `yb-data/tserver` data directory. Mandatory.

#### --fs_wal_dirs

The directory where the `yb-tserver` will place its write-ahead logs. May be the same as one of the directories listed in `--fs_data_dirs`, but not a sub-directory of a data directory.

Default: Same as `--fs_data_dirs`

#### --max_clock_skew_usec

Specifies the expected maximum clock skew between any two nodes in your deployment.

Default: `50000` (50ms)

#### --rpc_bind_addresses

Specifies the comma-separated list of addresses to bind to for RPC connections.

Default: `0.0.0.0:9100`

#### --server_broadcast_addresses

Public IP or DNS hostname of the server (with an optional port).

Default: `0.0.0.0:9100`

#### --use_private_ip

Determines when to use private IP addresses. Possible values are `never` (default),`zone`,`cloud` and `region`. Based on the values of the [placement (`--placement_*`) configuration options](#placement-options).

Default: `never`

#### --webserver_interface

Address to bind for the web server user interface.

Default: `0.0.0.0` (`127.0.0.1`)

#### --webserver_port

The port for monitoring the web server.

Default: `7000`

#### --webserver_doc_root

Monitoring web server home.

Default: The `www` directory in the YugabyteDB home directory.

### Logging options

#### --logtostderr

Flag to log to standard error (`stderr`).

#### --log_dir

Specifies the directory to store `yb-tserver` log files.

Default: Same as [`--fs_data_dirs`](#fs-data-dirs)

### Placement options

#### --placement_zone

Specifies the name of the availability zone, or rack, where this instance is deployed.

Default: `rack1`

#### --placement_region

Specifies the name of the region, or data center, where this instance is deployed.

Default: `datacenter1`

#### --placement_cloud

Specifies the name of the cloud where this instance is deployed.

Default: `cloud1`

### YSQL options

The following options, or flags, support the use of the [YSQL API](../../api/ysql/).

#### --enable_ysql

Enables the YSQL API. Replaces the deprecated `--start_pgsql_proxy` option.

Default: `false`

#### --ysql_enable_auth

Enables YSQL authentication. 

{{< note title="Note" >}}

**Yugabyte 2.0:** Assign a password for the default `yugabyte` user to be able to sign in after enabling YSQL authentication.

**Yugabyte 2.0.1:** When YSQL authentication is enabled, you can sign into `ysqlsh` using the default `yugabyte` user that has a default password of `yugabyte".

{{< /note >}}

Default: `false`

#### --pgsql_proxy_bind_address

Specifies the TCP/IP bind addresses for the YSQL API. The default value of `0.0.0.0:5433` allows listening for all IPv4 addresses access to localhost on port `5433`. The `--pgsql_proxy_bind_address` value overwrites `listen_addresses` (default value of `127.0.0.1:5433`) that controls which interfaces accept connection attempts.

To specify fine-grained access control over who can access the server, use [`--ysql_hba_conf`](#ysql-hba-conf).

Default: `0.0.0.0:5433`

{{< note title="Note" >}}

When using local YugabyteDB clusters built using the

{{< /note >}}

#### --pgsql_proxy_webserver_port

Specifies the web server port for YSQL metrics monitoring.

Default: `13000`

#### --ysql_hba_conf

Specifies a comma-separated list of PostgreSQL client authentication settings that is written to the `ysql_hba.conf` file.

For details on using `--ysql_hba_conf` to specify client authentication, see [Configure YSQL client authentication](../secure/authentication/ysql-client-authentication.md).

Default: `"host all all 0.0.0.0/0 trust,host all all ::0/0 trust"`

#### --ysql_pg_conf

Comma-separated list of PostgreSQL setting assignments.

#### --ysql_timezone

Specifies the time zone for displaying and interpreting timestamps.

Default: Uses the YSQL time zone.

#### --ysql_datestyle

Specifies the display format for data and time values.

Default: Uses the YSQL display format.

#### --ysql_max_connections

Specifies the maximum number of concurrent YSQL connections.

Default: `300`

#### --ysql_default_transaction_isolation

Specifies the default transaction isolation level.

Valid values: `READ UNCOMMITTED`, `READ COMMITTED`, `REPEATABLE READ`, and `SERIALIZABLE`.

Default: `READ COMMITTED` (implemented in YugabyteDB as `REPEATABLE READ`)

{{< note title="Note" >}}

YugabyteDB supports two transaction isolation levels: `REPEATABLE READ` (aka snapshot) and `SERIALIZABLE`. The transaction isolation levels of `READ UNCOMMITTED` and `READ COMMITTED` are implemented in YugabyteDB as `REPEATABLE READ`.

{{< /note >}}

#### --ysql_log_statement

Specifies the types of YSQL statements that should be logged.

#### --ysql_log_statement

Specifies the types of YSQL statements that should be logged.

#### --ysql_log_min_messages

Specifies the lowest YSQL message level to log.

### YCQL options

The following options, or flags, support the use of the [YCQL API](../../api/ycql/).

#### --use_cassandra_authentication

Specify `true` to enable YCQL authentication (`username` and `password`), enable YCQL security statements (`CREATE ROLE`, `DROP ROLE`, `GRANT ROLE`, `REVOKE ROLE`, `GRANT PERMISSION`, and `REVOKE PERMISSION`), and enforce permissions for YCQL statements.

Default: `false`

#### --cql_proxy_bind_address

Specifies the bind address for the YCQL API.

Default: `0.0.0.0:9042`

#### --cql_proxy_webserver_port

Specifies the port for monitoring YCQL metrics.

Default: `12000`

### YEDIS options

The following options, or flags, support the use of the YEDIS API.

#### --redis_proxy_bind_address

Specifies the bind address for the YEDIS API.

Default: `0.0.0.0:6379`

#### --redis_proxy_webserver_port

Specifies the port for monitoring YEDIS metrics.

Default: `11000`

### Performance options

#### --rocksdb_compact_flush_rate_limit_bytes_per_sec

Used to control rate of memstore flush and SSTable file compaction.

Default: `256MB`

#### --remote_bootstrap_rate_limit_bytes_per_sec

Rate control across all tablets being remote bootstrapped from or to this process.

Default: `256MB`

#### --yb_num_shards_per_tserver

The number of shards per YB-TServer per table when a user table is created.

Default: Server automatically picks a valid default internally, typically 8.

### Write Ahead Log (WAL) options

#### --durable_wal_write

If set to `false`, the writes to the Raft log are synced to disk every `interval_durable_wal_write_ms` milliseconds or every `bytes_durable_wal_write_mb` MB, whichever comes first. This default setting is recommended only for multi-AZ or multi-region deployments where the zones/regions are independent failure domains and there isn't a risk of correlated power loss. For single AZ deployments, this flag should be set to `true`.

Default: `false`

#### --interval_durable_wal_write_ms

When [`--durable_wal_write`](#durable-wal-write) is false, writes to the Raft log are synced to disk every `--interval_durable_wal_write_ms` or [`--bytes_durable_wal_write_mb`](#bytes-durable-wal-write-mb), whichever comes first.

#### --bytes_durable_wal_write_mb

When `--durable_wal_write` is `false`, writes to the Raft log are synced to disk every `--bytes_durable_wal_write_mb` or `--interval_durable_wal_write_ms`, whichever comes first.

Default: `1000ms` or `1MB`

## Admin UI

The Admin UI for the YB-TServer is available at `http://localhost:9000`.

### Home

Home page of the YB-TServer (`yb-tserver`) that gives a high level overview of this specific instance.

![tserver-home](/images/admin/tserver-home.png)

### Dashboards

List of all dashboards to review the ongoing operations

![tserver-dashboards](/images/admin/tserver-dashboards.png)

### Tablets

List of all tablets managed by this specific instance, sorted by the table name.

![tserver-tablets](/images/admin/tserver-tablets.png)

### Debug

List of all utilities available to debug the performance of this specific instance.

![tserver-debug](/images/admin/tserver-debug.png)

## Default ports reference

The various default ports are listed below.

Service | Type | Port
--------|------| -------
`yb-master` | rpc | 7100
`yb-master` | admin web server | 7000
`yb-tserver` | rpc | 9100
`yb-tserver` | admin web server | 9000
`ycql` | rpc | 9042
`ycql` | admin web server | 12000
`yedis` | rpc | 6379
`yedis` | admin web server | 11000
`ysql` | rpc | 5433
`ysql` | admin web server | 13000
