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

{{<nav/pages>}}
{{<nav/page title="General" href="../general-flags">}}
{{<nav/page title="Logging" href="../logging-flags">}}
{{<nav/page title="Memory" href="../memory-flags">}}
{{</nav/pages>}}

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

##### --callhome_enabled

Disable callhome diagnostics.

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
