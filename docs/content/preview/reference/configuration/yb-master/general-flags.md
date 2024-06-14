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

##### --enable_ysql

{{< note title="Note" >}}

Ensure that `enable_ysql` values in `yb-master` configurations match the values in `yb-tserver` configurations.

{{< /note >}}

Enables the YSQL API when value is `true`.

Default: `true`

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
