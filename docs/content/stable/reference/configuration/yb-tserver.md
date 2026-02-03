---
title: yb-tserver configuration reference
headerTitle: yb-tserver
linkTitle: yb-tserver
description: YugabyteDB Tablet Server (yb-tserver) binary and configuration flags to store and manage data for client applications.
menu:
  stable:
    identifier: yb-tserver
    parent: configuration
    weight: 2100
rightNav:
  hideH4: true
type: docs
body_class: configuration
---

The YugabyteDB Tablet Server ([YB-TServer](../../../architecture/yb-tserver/)) is a critical component responsible for managing data storage, processing client requests, and handling replication in a YugabyteDB cluster. It ensures data consistency, fault tolerance, and scalability by storing and serving data as tablets (sharded units of storage distributed across multiple nodes). Proper configuration of the YB-TServer is important to optimize performance, manage system resources effectively, help establish secure communication, and provide high availability (HA).

This reference provides detailed information about various flags available for configuring the YB-TServers in a YugabyteDB cluster. Use these flags to fine-tune the server's behavior according to your deployment requirements and performance objectives.

Use the yb-tserver binary and its flags to configure the YB-TServer server. The yb-tserver executable file is located in the `bin` directory of YugabyteDB home.

{{< note title="Setting flags in YugabyteDB Anywhere" >}}

If you are using YugabyteDB Anywhere, set flags using the [Edit Flags](../../../yugabyte-platform/manage-deployments/edit-config-flags/#modify-configuration-flags) feature.

{{< /note >}}

Flags are organized in the following categories.

| Category                     | Description |
|------------------------------|-------------|
| [General configuration](#general-configuration)        | Basic server setup including overall system settings, logging, and web interface configurations. |
| [PostgreSQL configuration parameters](#postgresql-configuration-parameters)        | PostgreSQL settings, and YSQL-specific configuration parameters. |
| [Networking](#networking)                   | Flags that control network interfaces, RPC endpoints, DNS caching, and geo-distribution settings. |
| [Storage and data management](#storage-data-management)    | Parameters for managing data directories, WAL configurations, sharding, CDC, and TTL-based file expiration. |
| [Performance tuning](#performance-tuning)           | Options for resource allocation, memory management, compaction settings, and overall performance optimizations. |
| [Security](#security)                     | Settings for encryption, SSL/TLS, and authentication to secure both node-to-node and client-server communications. |

**Legend**

Flags with specific characteristics are highlighted using the following badges:

- {{% tags/feature/restart-needed %}} – A restart of the server is required for the flag to take effect. For example, if the flag is used on *yb-master*, you need to restart only *yb-master*. If the flag is used on both the *yb-master* and *yb-tserver*, restart both services.
- {{% tags/feature/t-server %}} – The flag must have the same value across all *yb-master* and *yb-tserver* nodes.

**Syntax**

```sh
yb-tserver [ flags ]
```

**Example**

```sh
./bin/yb-tserver \
    --tserver_master_addrs 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
    --rpc_bind_addresses 172.151.17.130 \
    --enable_ysql \
    --fs_data_dirs "/home/centos/disk1,/home/centos/disk2" &
```

**Online help**

To display the online help, run `yb-tserver --help` from the YugabyteDB home directory:

```sh
./bin/yb-tserver --help
```

Use `--helpon` to display help on modules named by the specified flag value.

## All flags

The following sections describe the flags considered relevant to configuring YugabyteDB for production deployments. For a list of all flags, see [All YB-TServer flags](../all-flags-yb-tserver/).

## General Configuration

##### --flagfile

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
{{% /tags/wrap %}}

Specifies the file to load the configuration flags from. The configuration flags must be in the same format as supported by the command line flags.

##### --version

Shows version and build info, then exits.

##### --max_clock_skew_usec

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `500000` (500,000 µs = 500ms)
{{% /tags/wrap %}}

Specifies the expected maximum clock skew, in microseconds (µs), between any two nodes in your deployment.

##### --tablet_server_svc_queue_length

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `5000`
{{% /tags/wrap %}}

Specifies the queue size for the tablet server to serve reads and writes from applications.

##### --time_source

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
{{<tags/feature/ea idea="1807">}}
Default: `""`
{{% /tags/wrap %}}

Specifies the time source used by the database. Set this to `clockbound` for configuring a highly accurate time source. Using `clockbound` requires [system configuration](../../../deploy/manual-deployment/system-config/#set-up-time-synchronization).

### Webserver configuration

##### --webserver_interface

{{% tags/wrap %}}


Default: `0.0.0.0` (`127.0.0.1`)
{{% /tags/wrap %}}

The address to bind for the web server user interface.

##### --webserver_port

{{% tags/wrap %}}


Default: `9000`
{{% /tags/wrap %}}

The port for monitoring the web server.

##### --webserver_doc_root

{{% tags/wrap %}}


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

Domain used for `.htpasswd` authentication. This should be used in conjunction with [`--webserver_password_file`](#webserver-password-file).

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
Default: The same as [`--fs_data_dirs`](#fs-data-dirs)
{{% /tags/wrap %}}

The directory to write yb-tserver log files.

##### --logtostderr

{{% tags/wrap %}}


Default: `false`
{{% /tags/wrap %}}

Write log messages to `stderr` instead of `logfiles`.

##### --max_log_size

{{% tags/wrap %}}


Default: `1800` (1.8 GB)
{{% /tags/wrap %}}

The maximum log size, in megabytes (MB). A value of `0` will be silently overridden to `1`.

##### --minloglevel

{{% tags/wrap %}}


Default: `0` (INFO)
{{% /tags/wrap %}}

The minimum level to log messages. Values are: `0` (INFO), `1`, `2`, `3` (FATAL).

##### --stderrthreshold

{{% tags/wrap %}}


Default: `3`
{{% /tags/wrap %}}

Log messages at, or above, this level are copied to `stderr` in addition to log files.

##### --stop_logging_if_full_disk

{{% tags/wrap %}}


Default: `false`
{{% /tags/wrap %}}

Stop attempting to log to disk if the disk is full.

##### --callhome_enabled

{{% tags/wrap %}}


Default: `true`
{{% /tags/wrap %}}

Disable callhome diagnostics.

### Metric export flags

YB-TServer metrics are available in Prometheus format at `http://localhost:9000/prometheus-metrics`.

##### --export_help_and_type_in_prometheus_metrics

{{% tags/wrap %}}


Default: `true`
{{% /tags/wrap %}}

This flag controls whether #TYPE and #HELP information is included as part of the Prometheus metrics output by default.

To override this flag on a per-scrape basis, set the URL parameter `show_help` to `true` to include or to `false` to not include type and help information.  For example, querying `http://localhost:9000/prometheus-metrics?show_help=true` returns type and help information regardless of the setting of this flag.

##### --max_prometheus_metric_entries

{{% tags/wrap %}}


Default: `UINT32_MAX`
{{% /tags/wrap %}}

Introduced in version 2.21.1.0, this flag limits the number of Prometheus metric entries returned per scrape. If adding a metric with all its entities exceeds this limit, all entries from that metric are excluded. This could result in fewer entries than the set limit.

To override this flag on a per-scrape basis, you can adjust the URL parameter `max_metric_entries`.

##### --export_intentdb_metrics

{{% tags/wrap %}}

Default: `true`
{{% /tags/wrap %}}

Whether to dump IntentsDB statistics to Prometheus metrics.

## PostgreSQL configuration parameters

YugabyteDB uses [PostgreSQL server configuration parameters](https://www.postgresql.org/docs/15/config-setting.html) to apply server configuration settings to new server instances.

### How to modify configuration parameters

The methods for setting configurations are listed in order of precedence, from lowest to highest. That is, explicitly setting values for a configuration parameter using methods further down the following list have higher precedence than earlier methods.

For example, if you set a parameter explicitly using both the YSQL flag (`ysql_<parameter>`), and in the PostgreSQL server configuration flag (`ysql_pg_conf_csv`), the YSQL flag takes precedence.

#### Methods

- Use the PostgreSQL server configuration flag [ysql_pg_conf_csv](#ysql-pg-conf-csv).

    For example, `--ysql_pg_conf_csv=yb_bnl_batch_size=512`.

- If a flag is available with the same parameter name and the `ysql_` prefix, then set the flag directly.

    For example, `--ysql_yb_bnl_batch_size=512`.

- Set the option per-database:

    ```sql
    ALTER DATABASE database_name SET yb_bnl_batch_size=512;
    ```

- Set the option per-role:

    ```sql
    ALTER ROLE yugabyte SET yb_bnl_batch_size=512;
    ```

- Set the option for a specific database and role:

    ```sql
    ALTER ROLE yugabyte IN DATABASE yugabyte SET yb_bnl_batch_size=512;
    ```

    Parameters set at the role or database level only take effect on new sessions.

- Set the option for the current session:

    ```sql
    SET yb_bnl_batch_size=512;
    --- alternative way
    SET SESSION yb_bnl_batch_size=512;
    ```

    If `SET` is issued in a transaction that is aborted later, the effects of the SET command are reverted when the transaction is rolled back.

    If the surrounding transaction commits, the effects will persist for the whole session.

- Set the option for the current transaction:

    ```sql
    SET LOCAL yb_bnl_batch_size=512;
    ```

- Set the option within the scope of a function or procedure:

    ```sql
    ALTER FUNCTION add_new SET yb_bnl_batch_size=512;
    ```

For information on available PostgreSQL server configuration parameters, refer to [Server Configuration](https://www.postgresql.org/docs/15/runtime-config.html) in the PostgreSQL documentation.

### YSQL configuration parameters

The server configuration parameters for YugabyteDB are the same as for PostgreSQL, with the following exceptions and additions.

Note that if a corresponding flag is available (with the same name as the parameter and `ysql_` prefix), then set the flag directly.

##### log_line_prefix

YugabyteDB supports the following additional options for the `log_line_prefix` parameter:

- %C = cloud name
- %R = region / data center name
- %Z = availability zone / rack name
- %U = cluster UUID
- %N = node and cluster name
- %H = current hostname

For information on using `log_line_prefix`, refer to [log_line_prefix](https://www.postgresql.org/docs/15/runtime-config-logging.html#GUC-LOG-LINE-PREFIX) in the PostgreSQL documentation.

##### suppress_nonpg_logs (boolean)

{{% tags/wrap %}}


Default: `off`
{{% /tags/wrap %}}

When set, suppresses logging of non-PostgreSQL output to the PostgreSQL log file in the `tserver/logs` directory.

##### temp_file_limit

{{% tags/wrap %}}


Default: `1GB`
{{% /tags/wrap %}}

Specifies the amount of disk space used for temporary files for each YSQL connection, such as sort and hash temporary files, or the storage file for a held cursor.

Any query whose disk space usage exceeds `temp_file_limit` will terminate with the error `ERROR:  temporary file size exceeds temp_file_limit`. Note that temporary tables do not count against this limit.

You can remove the limit (set the size to unlimited) using `temp_file_limit=-1`.

Valid values are `-1` (unlimited), `integer` (in kilobytes), `nMB` (in megabytes), and `nGB` (in gigabytes) (where 'n' is an integer).

##### enable_bitmapscan

{{% tags/wrap %}}


Default: `true`
{{% /tags/wrap %}}

PostgreSQL parameter to enable or disable the query planner's use of bitmap-scan plan types.

Bitmap Scans use multiple indexes to answer a query, with only one scan of the main table. Each index produces a "bitmap" indicating which rows of the main table are interesting. Multiple bitmaps can be combined with `AND` or `OR` operators to create a final bitmap that is used to collect rows from the main table.

Bitmap scans follow the same `work_mem` behavior as PostgreSQL: each individual bitmap is bounded by `work_mem`. If there are n bitmaps, it means we may use `n * work_mem` memory.

Bitmap scans are only supported for LSM indexes.

##### yb_enable_bitmapscan

{{% tags/wrap %}}

Default: `false`

{{% /tags/wrap %}}

Enables or disables the query planner's use of bitmap scans for YugabyteDB relations.

In v2025.2 and later, bitmap scan is enabled by default in new universes when you deploy using yugabyted, YugabyteDB Anywhere, or YugabyteDB Aeon.

In addition, when upgrading a deployment to v2025.2 or later, if the universe has the cost-based optimizer enabled (`on`), YugabyteDB will enable bitmap scan.

Both [enable_bitmapscan](#enable-bitmapscan) and `yb_enable_bitmapscan` must be set to true for a YugabyteDB relation to use a bitmap scan. If `yb_enable_bitmapscan` is false, the planner never uses a YugabyteDB bitmap scan.

| enable_bitmapscan | yb_enable_bitmapscan | Result |
| :--- | :---  | :--- |
| true | false | Default. Bitmap scans allowed only on temporary tables, if the planner believes the bitmap scan is most optimal. |
| true | true  | Default for [Enhanced PostgreSQL Compatibility](../postgresql-compatibility/). Bitmap scans are allowed on temporary tables and YugabyteDB relations, if the planner believes the bitmap scan is most optimal. |
| false | false | Bitmap scans allowed only on temporary tables, but only if every other scan type is also disabled / not possible. |
| false | true  | Bitmap scans allowed on temporary tables and YugabyteDB relations, but only if every other scan type is also disabled / not possible. |

##### yb_bnl_batch_size

{{% tags/wrap %}}


Default: `1024`
{{% /tags/wrap %}}

Set the size of a tuple batch that's taken from the outer side of a [batched nested loop (BNL) join](../../../architecture/query-layer/join-strategies/#batched-nested-loop-join-bnl). When set to 1, BNLs are effectively turned off and won't be considered as a query plan candidate.

Can be set using the [--ysql_yb_bnl_batch_size](#ysql-yb-bnl-batch-size) flag.

##### yb_enable_batchednl

{{% tags/wrap %}}


Default: `true`
{{% /tags/wrap %}}

Enable or disable the query planner's use of batched nested loop join.

##### yb_enable_cbo

{{% tags/wrap %}}

Default: `legacy_mode`
{{% /tags/wrap %}}

Enables the YugabyteDB [cost-based optimizer](../../../architecture/query-layer/planner-optimizer/) (CBO). Options are `on`, `off`, `legacy_mode`, and `legacy_stats_mode`.

In v2025.2 and later, CBO is enabled by default (`on`) in new universes when you deploy using yugabyted, YugabyteDB Anywhere, or YugabyteDB Aeon.

This parameter replaces the [yb_enable_base_scans_cost_model](#yb-enable-base-scans-cost-model) and [yb_enable_optimizer_statistics](#yb-enable-optimizer-statistics) parameters.

When enabling CBO, you must run ANALYZE on user tables to maintain up-to-date statistics.

For information on using this parameter to configure CBO, refer to [Enable cost-based optimizer](../../../best-practices-operations/ysql-yb-enable-cbo/).

See also the [--ysql_yb_enable_cbo](#ysql-yb-enable-cbo) flag. If the flag is set, this parameter takes precedence.

##### yb_enable_base_scans_cost_model

{{% tags/wrap %}}

Default: `false`
{{% /tags/wrap %}}

Enables the YugabyteDB cost model for Sequential and Index scans. When enabling this parameter, you must run ANALYZE on user tables to maintain up-to-date statistics.

Note: this parameter has been replaced by [yb_enable_cbo](#yb-enable-cbo).

##### yb_enable_optimizer_statistics

{{% tags/wrap %}}
{{<tags/feature/tp>}}
Default: `false`
{{% /tags/wrap %}}

Enables use of the PostgreSQL selectivity estimation, which uses table statistics collected with ANALYZE.

Note: this parameter has been replaced by [yb_enable_cbo](#yb-enable-cbo).

##### yb_fetch_size_limit

{{% tags/wrap %}}
{{<tags/feature/tp>}}
Default: `0`
{{% /tags/wrap %}}

Maximum size (in bytes) of total data returned in one response when the query layer fetches rows of a table from DocDB. Used to bound how many rows can be returned in one request. Set to 0 to have no size limit. To enable size based limit, `yb_fetch_row_limit` should be set to 0.

If both `yb_fetch_row_limit` and `yb_fetch_size_limit` are set then limit is taken as the lower bound of the two values.

See also the [--ysql_yb_fetch_size_limit](#ysql-yb-fetch-size-limit) flag. If the flag is set, this parameter takes precedence.

##### yb_fetch_row_limit

{{% tags/wrap %}}
{{<tags/feature/tp>}}
Default: `1024`
{{% /tags/wrap %}}

Maximum number of rows returned in one response when the query layer fetches rows of a table from DocDB. Used to bound how many rows can be returned in one request. Set to 0 to have no row limit.

See also the [--ysql_yb_fetch_row_limit](#ysql-yb-fetch-row-limit) flag. If the flag is set, this parameter takes precedence.

##### yb_use_hash_splitting_by_default

{{% tags/wrap %}}
{{<tags/feature/ea>}}
{{<tags/feature/restart-needed>}}
Default: `true`
{{% /tags/wrap %}}

When set to true, tables and indexes are hash-partitioned based on the first column in the primary key or index. Setting this flag to false changes the first column in the primary key or index to be stored in ascending order.

##### yb_insert_on_conflict_read_batch_size

{{% tags/wrap %}}

{{<tags/feature/restart-needed>}}
Default: `0` (disabled)
{{% /tags/wrap %}}

Set the level of batching for [INSERT ... ON CONFLICT](../../../api/ysql/the-sql-language/statements/dml_insert/#on-conflict-clause). Set to 0 to disable batching. Batching is always disabled for the following:

- temporary relations
- foreign relations
- system relations
- relations that have row triggers (excluding those created internally for FOREIGN KEY constraints)

The higher the number, the more batching is done. 1024 is recommended.

##### yb_read_from_followers

{{% tags/wrap %}}


Default: `false`
{{% /tags/wrap %}}

Controls whether or not reading from followers is enabled. For more information, refer to [Follower reads](../../../explore/going-beyond-sql/follower-reads-ysql/).

##### yb_follower_read_staleness_ms

{{% tags/wrap %}}


Default: `30000` (30 seconds)
{{% /tags/wrap %}}

Sets the maximum allowable staleness. Although the default is recommended, you can set the staleness to a shorter value. The tradeoff is the shorter the staleness, the more likely some reads may be redirected to the leader if the follower isn't sufficiently caught up. You shouldn't set `yb_follower_read_staleness_ms` to less than 2x the `raft_heartbeat_interval_ms` (which by default is 500 ms).

##### default_transaction_read_only

{{% tags/wrap %}}


Default: `false`
{{% /tags/wrap %}}

Turn this setting `ON/TRUE/1` to make all the transactions in the current session read-only. This is helpful when you want to run reports or set up [follower reads](../../../explore/going-beyond-sql/follower-reads-ysql/#read-only-transaction).

##### default_transaction_isolation

{{% tags/wrap %}}


Default: `'read committed'`
{{% /tags/wrap %}}

Specifies the default isolation level of each new transaction. Every transaction has an isolation level of `'read uncommitted'`, `'read committed'`, `'repeatable read'`, or `serializable`.

For example:

```sql
SET default_transaction_isolation='repeatable read';
```

See [transaction isolation levels](../../../architecture/transactions/isolation-levels) for reference.

See also the [--ysql_default_transaction_isolation](#ysql-default-transaction-isolation) flag.

##### yb_skip_redundant_update_ops

{{% tags/wrap %}}


Default: `true`
{{% /tags/wrap %}}

Enables skipping updates to columns that are part of secondary indexes and constraint checks when the column values remain unchanged.

This parameter can only be configured during cluster startup, and adjusting this parameter does not require a cluster restart.

##### yb_read_time

{{% tags/wrap %}}


Default: `0`
{{% /tags/wrap %}}

Enables [time travel queries](../../../manage/backup-restore/time-travel-query/) by specifying a Unix timestamp. After setting the parameter, all subsequent read queries are executed as of that read time, in the current session. Other YSQL sessions are not affected.

To reset the session to normal behavior (current time), set `yb_read_time` to 0.

Write DML queries (INSERT, UPDATE, DELETE) and DDL queries are not allowed in a session that has a read time in the past.

##### yb_sampling_algorithm

{{% tags/wrap %}}

Default: block_based_sampling
{{% /tags/wrap %}}

Determines the sampling algorithm to use to select random rows from a table when performing sampling operations in YSQL. You can choose from the following algorithms:

- `full_table_scan`: Scans the whole table and picks random rows
- `block_based_sampling`: Samples the table for a set of blocks, and then scans only those selected blocks to form a final rows sample.

##### yb_locks_min_txn_age

{{% tags/wrap %}}

Default: `1`
{{% /tags/wrap %}}

Specifies the minimum age of a transaction (in seconds) before its locks are included in the results returned from querying the [pg_locks](../../../explore/observability/pg-locks/) view. Use this parameter to focus on older transactions that may be more relevant to performance tuning or deadlock resolution efforts.

##### yb_locks_max_transactions

{{% tags/wrap %}}

Default: `16`
{{% /tags/wrap %}}

Sets the maximum number of transactions for which lock information is displayed when you query the [pg_locks](../../../explore/observability/pg-locks/) view. Limits output to the most relevant transactions, which is particularly beneficial in environments with high levels of concurrency and transactional activity.

##### yb_locks_txn_locks_per_tablet

{{% tags/wrap %}}

Default: `200`
{{% /tags/wrap %}}

Sets the maximum number of rows per transaction per tablet to return in [pg_locks](../../../explore/observability/pg-locks/). Set to 0 to return all results.

##### yb_default_copy_from_rows_per_transaction

{{% tags/wrap %}}

Default: `20000`
{{% /tags/wrap %}}

Sets the maximum batch size per transaction when using [COPY FROM](../../../api/ysql/the-sql-language/statements/cmd_copy/).

#### Bucket-based index scan optimization

##### yb_enable_derived_equalities

{{% tags/wrap %}}
{{<tags/feature/tp idea="2275">}}
Default: `false`
{{% /tags/wrap %}}

Enables derivation of additional equalities for columns that are generated or computed using an expression. Used for [bucket-based indexes](../../../develop/data-modeling/bucket-index-ysql/).

##### yb_enable_derived_saops

{{% tags/wrap %}}
{{<tags/feature/tp idea="2275">}}
Default: `false`
{{% /tags/wrap %}}

Enable derivation of IN clauses for columns generated or computed using a `yb_hash_code` expression. Such derivation is only done for index paths that consider bucket-based merge. Disabled if `yb_max_saop_merge_streams` is 0.

##### yb_max_saop_merge_streams

{{% tags/wrap %}}
{{<tags/feature/tp idea="2275">}}
Default: `0`
{{% /tags/wrap %}}

Maximum number of buckets to process in parallel. A value greater than 0 enables bucket-based merge (used for [bucket-based indexes](../../../develop/data-modeling/bucket-index-ysql/)). Disabled if the cost-based optimizer is not enabled (`yb_enable_cbo=false`). Recommended value is 64.

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
{{% /tags/wrap %}}

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

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `""`
{{% /tags/wrap %}}

Specifies the public IP or DNS hostname of the server (with an optional port). This value is used by servers to communicate with one another, depending on the connection policy parameter.

### Private IP and DNS caching

##### --dns_cache_expiration_ms

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
Default: `60000` (1 minute)
{{% /tags/wrap %}}

Specifies the duration, in milliseconds, until a cached DNS resolution expires. When hostnames are used instead of IP addresses, a DNS resolver must be queried to match hostnames to IP addresses. By using a local DNS cache to temporarily store DNS lookups, DNS queries can be resolved quicker and additional queries can be avoided, thereby reducing latency, improving load times, and reducing bandwidth and CPU consumption.

If you change this value from the default, be sure to add the identical value to all YB-Master and YB-TServer configurations.

##### --use_private_ip

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `never`
{{% /tags/wrap %}}

Specifies the policy that determines when to use private IP addresses for inter-node communication. Possible values are `never`, `zone`, `cloud`, and `region`. Based on the values of the [geo-distribution flags](#geo-distribution-flags).

Valid values for the policy are:

- `never` — Always use [`--server_broadcast_addresses`](#server-broadcast-addresses).
- `zone` — Use the private IP inside a zone; use [`--server_broadcast_addresses`](#server-broadcast-addresses) outside the zone.
- `cloud` — Use the private IP address across all zones in a cloud; use [`--server_broadcast_addresses`](#server-broadcast-addresses) outside the cloud.
- `region` — Use the private IP address across all zone in a region; use [`--server_broadcast_addresses`](#server-broadcast-addresses) outside the region.

### Geo-distribution flags

Settings related to managing geo-distributed clusters:

##### --placement_zone

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `rack1`
{{% /tags/wrap %}}

The name of the availability zone, or rack, where this instance is deployed.

{{<tip title="Rack awareness">}}
For on-premises deployments, consider racks as zones to treat them as fault domains.
{{</tip>}}

##### --placement_region

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `datacenter1`
{{% /tags/wrap %}}

Specifies the name of the region, or data center, where this instance is deployed.

##### --placement_cloud

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `cloud1`
{{% /tags/wrap %}}

Specifies the name of the cloud where this instance is deployed.

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

##### --auto-create-local-transaction-tables

{{% tags/wrap %}}


Default: `true`
{{% /tags/wrap %}}

If true, transaction status tables will be created under each YSQL tablespace that has a placement set and contains at least one other table.

##### --auto-promote-nonlocal-transactions-to-global

{{% tags/wrap %}}


Default: `true`
{{% /tags/wrap %}}

If true, local transactions using transaction status tables other than `system.transactions` will be automatically promoted to global transactions using the `system.transactions` transaction status table upon accessing data outside of the local region.

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

## Storage & Data Management

### Filesystem and WAL directories

##### --fs_data_dirs

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
{{% /tags/wrap %}}

Specifies a comma-separated list of mount directories, where yb-tserver will add a `yb-data/tserver` data directory, `tserver.err`, `tserver.out`, and `pg_data` directory.

Required.

Changing the value of this flag after the cluster has already been created is not supported.

##### --fs_wal_dirs

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: The same value as `--fs_data_dirs`
{{% /tags/wrap %}}

Specifies a comma-separated list of directories, where yb-tserver will store write-ahead (WAL) logs. This can be the same as one of the directories listed in `--fs_data_dirs`, but not a subdirectory of a data directory.

### Write ahead log (WAL) flags

In a typical deployment, the values used for write ahead log (WAL) flags in `yb-tserver` configurations should align with those in [`yb-master`](../yb-master/#raft-flags) configurations.

Ensure that values used for the write ahead log (WAL) in yb-tserver configurations match the values for yb-master configurations.

##### --fs_wal_dirs

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
{{<tags/feature/restart-needed>}}
Default: The same as `--fs_data_dirs`
{{% /tags/wrap %}}

The directory where the yb-tserver retains WAL files. May be the same as one of the directories listed in [--fs_data_dirs](#fs-data-dirs), but not a subdirectory of a data directory.

##### --durable_wal_write

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

If set to `false`, the writes to the WAL are synchronized to disk every [interval_durable_wal_write_ms](#interval-durable-wal-write-ms) milliseconds (ms) or every [bytes_durable_wal_write_mb](#bytes-durable-wal-write-mb) megabyte (MB), whichever comes first. This default setting is recommended only for multi-AZ or multi-region deployments where the availability zones (AZs) or regions are independent failure domains and there is not a risk of correlated power loss. For single AZ deployments, this flag should be set to `true`.

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
Default: `900` (15 minutes)
{{% /tags/wrap %}}

The minimum duration, in seconds, to retain WAL segments, regardless of durability requirements. WAL segments can be retained for a longer amount of time, if they are necessary for correct restart. This value should be set long enough such that a tablet server which has temporarily failed can be restarted in the given time period.

##### --log_min_segments_to_retain

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
Default: `2`
{{% /tags/wrap %}}

The minimum number of WAL segments (files) to retain, regardless of durability requirements. The value must be at least `1`.

##### --log_segment_size_mb

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
Default: `64`
{{% /tags/wrap %}}

The size, in megabytes (MB), of a WAL segment (file). When the WAL segment reaches the specified size, then a log rollover occurs and a new WAL segment file is created.

##### --reuse_unclosed_segment_threshold_bytes

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
Default: `524288` (0.5 MB)
{{% /tags/wrap %}}

When the server restarts from a previous crash, if the tablet's last WAL file size is less than or equal to this threshold value, the last WAL file will be reused. Otherwise, WAL will allocate a new file at bootstrap. To disable WAL reuse, set the value to `-1`.

Default: The default value in {{<release "2.18.1">}} is `-1` - feature is disabled by default. The default value starting from {{<release "2.19.1">}} is `524288` (0.5 MB) - feature is enabled by default.

### Sharding flags

##### --yb_num_shards_per_tserver

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
Default: `-1`
{{% /tags/wrap %}}

The number of shards (tablets) per YB-TServer for each YCQL table when a user table is created.

The number of shards is determined at runtime, as follows:

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
- If the value is set to _Default_ (`-1`), then the system automatically determines an appropriate value based on the number of CPU cores and internally _updates_ the flag with the intended value during startup prior to version 2.18 and the flag remains _unchanged_ starting from version 2.18.
- The [CREATE TABLE ... WITH TABLETS = <num>](../../../api/ycql/ddl_create_table/#create-a-table-specifying-the-number-of-tablets) clause can be used on a per-table basis to override the `yb_num_shards_per_tserver` value.

{{< /note >}}

##### --ysql_num_shards_per_tserver

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
Default: `-1`
{{% /tags/wrap %}}

The number of shards (tablets) per YB-TServer for each YSQL table when a user table is created.

The number of shards is determined at runtime, as follows:

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

{{% tags/wrap %}}


Default: `60`
{{% /tags/wrap %}}

Interval at which the tablet manager tries to cleanup split tablets that are no longer needed. Setting this to 0 disables cleanup of split tablets.

##### --enable_automatic_tablet_splitting

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
Default: `true`
{{% /tags/wrap %}}

Enables YugabyteDB to [automatically split tablets](../../../architecture/docdb-sharding/tablet-splitting/#automatic-tablet-splitting).

{{< note title="Important" >}}

This value must match on all yb-master and yb-tserver configurations of a YugabyteDB cluster.

{{< /note >}}

##### --ysql_colocate_database_by_default

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

When enabled, all databases created in the cluster are colocated by default. If you enable the flag after creating a cluster, you need to restart the YB-Master and YB-TServer services.

For more details, see [clusters in colocated tables](../../../additional-features/colocation/).

##### tablet_replicas_per_core_limit

{{% tags/wrap %}}


Default: `0` for no limit.
{{% /tags/wrap %}}

The number of tablet replicas that each core on a YB-TServer can support.

##### tablet_replicas_per_gib_limit

{{% tags/wrap %}}


Default: 1024 * (7/10) (corresponding to an overhead of roughly 700 KiB per tablet)
{{% /tags/wrap %}}

The number of tablet replicas that each GiB reserved by YB-TServers for tablet overheads can support.

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

To learn about CDC, see [Change data capture (CDC)](../../../additional-features/change-data-capture/).

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

##### --cdc_max_stream_intent_records

{{% tags/wrap %}}


Default: `1680`
{{% /tags/wrap %}}

Maximum number of intent records allowed in a single CDC batch.

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


Default: `60000`
{{% /tags/wrap %}}

{{< warning title="Warning" >}}

If you are using multiple streams, it is advised that you set this flag to `1800000` (30 minutes).

{{< /warning >}}

##### --log_max_seconds_to_retain

{{% tags/wrap %}}


Default: `86400`
{{% /tags/wrap %}}

Number of seconds to retain log files. Log files older than this value will be deleted even if they contain unreplicated CDC entries. If 0, this flag will be ignored. This flag is ignored if a log segment contains entries that haven't been flushed to RocksDB.

##### --log_stop_retaining_min_disk_mb

{{% tags/wrap %}}


Default: `102400`
{{% /tags/wrap %}}

Stop retaining logs if the space available for the logs falls below this limit, specified in megabytes. As with `log_max_seconds_to_retain`, this flag is ignored if a log segment contains unflushed entries.

##### --cdc_intent_retention_ms

{{% tags/wrap %}}


Default: `28800000` (8 hours)
{{% /tags/wrap %}}

The time period, in milliseconds, after which the intents will be cleaned up if there is no client polling for the change records.

##### --cdc_wal_retention_time_secs

{{% tags/wrap %}}


Default: `28800` (8 hours)
{{% /tags/wrap %}}

WAL retention time, in seconds, to be used for tables for which a CDC stream was created. Used in both xCluster and CDCSDK.

##### --cdcsdk_table_processing_limit_per_run

{{% tags/wrap %}}


Default: `2`
{{% /tags/wrap %}}

Number of tables to be added to the stream ID per run of the background thread which adds newly created tables to the active streams on its namespace.

The following set of flags are only relevant for CDC using the PostgreSQL replication protocol. To learn about CDC using the PostgreSQL replication protocol, see [CDC using logical replication](../../../architecture/docdb-replication/cdc-logical-replication).

##### --ysql_yb_default_replica_identity

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `CHANGE`
{{% /tags/wrap %}}

The default replica identity to be assigned to user-defined tables at the time of creation. The flag is case sensitive and can take only one of the four possible values, `FULL`, `DEFAULT`, `NOTHING`, and `CHANGE`.

For more information, refer to [Replica identity](../../../additional-features/change-data-capture/using-logical-replication/yugabytedb-connector/#replica-identity).

##### --cdcsdk_enable_dynamic_table_support

{{% tags/wrap %}}


Default: `true`
{{% /tags/wrap %}}

Tables created after the creation of a replication slot are referred as Dynamic tables. This flag can be used to switch the dynamic addition of tables to the publication ON or OFF.

##### --cdcsdk_publication_list_refresh_interval_secs

{{% tags/wrap %}}


Default: `900`
{{% /tags/wrap %}}

Interval in seconds at which the table list in the publication will be refreshed.

##### --cdc_stream_records_threshold_size_bytes

{{% tags/wrap %}}


Default: `4194304` (4MB)
{{% /tags/wrap %}}

Maximum size (in bytes) of changes from a tablet sent from the CDC service to the gRPC connector when using the gRPC replication protocol.

Maximum size (in bytes) of changes sent from the [Virtual WAL](../../../architecture/docdb-replication/cdc-logical-replication) (VWAL) to the Walsender process when using the PostgreSQL replication protocol.

##### --cdcsdk_vwal_getchanges_resp_max_size_bytes

{{% tags/wrap %}}


Default: `1 MB`
{{% /tags/wrap %}}

Max size (in bytes) of changes sent from CDC Service to [Virtual WAL](../../../architecture/docdb-replication/cdc-logical-replication)(VWAL) for a particular tablet.

##### --ysql_cdc_active_replication_slot_window_ms

{{% tags/wrap %}}


Default: `60000`
{{% /tags/wrap %}}

Determines the window in milliseconds in which if a client has consumed the changes of a ReplicationSlot across any tablet, then it is considered to be actively used. ReplicationSlots which haven't been used in this interval are considered to be inactive.

##### --cdc_send_null_before_image_if_not_exists

{{% tags/wrap %}}


Default: `false`
{{% /tags/wrap %}}

When true, the CDC service returns a null before-image if it is not able to find one.

##### --cdcsdk_tablet_not_of_interest_timeout_secs

{{% tags/wrap %}}


Default: `14400` (4 hours)
{{% /tags/wrap %}}

Timeout after which it is inferred that a particular tablet is not of interest for CDC. To indicate that a particular tablet is of interest for CDC, it should be polled at least once within this interval of stream / slot creation.

##### --timestamp_syscatalog_history_retention_interval_sec

{{% tags/wrap %}}

Default: `4 * 3600` (4 hours)
{{% /tags/wrap %}}

The time interval, in seconds, to retain history/older versions of the system catalog.

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

### xCluster flags

Settings related to managing xClusters.

##### --xcluster_svc_queue_size

{{% tags/wrap %}}


Default: `5000`
{{% /tags/wrap %}}

The RPC queue size of the xCluster service. Should match the size of [tablet_server_svc_queue_length](#tablet-server-svc-queue-length) used for read and write requests.

### Packed row flags

The packed row format for the YSQL API is {{<tags/feature/ga>}} as of v2.20.0, and for the YCQL API is {{<tags/feature/tp>}}.

To learn about the packed row feature, see [Packed rows in DocDB](../../../architecture/docdb/packed-rows) in the architecture section.

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


Default: `0`
{{% /tags/wrap %}}

Packed row size limit for YCQL. The default value is 0 (use block size as limit). For rows that are over this size limit, a greedy approach will be used to pack as many columns as possible, with the remaining columns stored as individual key-value pairs.

### Catalog flags

For information on setting these flags, see [Customize preloading of YSQL catalog caches](../../../best-practices-operations/ysql-catalog-cache-tuning-guide/).

##### --ysql_catalog_preload_additional_table_list

{{% tags/wrap %}}


Default: `""`
{{% /tags/wrap %}}

Specifies the names of catalog tables (such as `pg_operator`, `pg_proc`, and `pg_amop`) to be preloaded by PostgreSQL backend processes. This flag reduces latency of first query execution of a particular statement on a connection.

If [ysql_catalog_preload_additional_tables](#ysql-catalog-preload-additional-tables) is also specified, the union of the above specified catalog tables and `pg_am`, `pg_amproc`, `pg_cast`, and `pg_tablespace` is preloaded.

##### --ysql_catalog_preload_additional_tables

{{% tags/wrap %}}


Default: `false`
{{% /tags/wrap %}}

When enabled, the PostgreSQL backend processes preload the `pg_am`, `pg_amproc`, `pg_cast`, and `pg_tablespace` catalog tables. This flag reduces latency of first query execution of a particular statement on a connection.

If [ysql_catalog_preload_additional_table_list](#ysql-catalog-preload-additional-table-list) is also specified, the union of `pg_am`, `pg_amproc`, `pg_cast`, and `pg_tablespace` and the tables specified in `ysql_catalog_preload_additional_table_list` is preloaded.

##### --ysql_enable_read_request_caching

{{% tags/wrap %}}


Default: `true`
{{% /tags/wrap %}}

Enables the YB-TServer catalog cache, which reduces YB-Master overhead for starting a connection and internal system catalog metadata refresh (for example, after executing a DDL), when there are many YSQL connections per node.

##### --ysql_minimal_catalog_caches_preload

{{% tags/wrap %}}


Default: `false`
{{% /tags/wrap %}}

Defines what part of the catalog gets cached and preloaded by default. As a rule of thumb, preloading more means lower first-query latency (as most/all necessary metadata will already be in the cache) at a cost of higher per-connection memory. Preloading less of the catalog means less memory though can result in a higher mean first-query latency (as we may need to ad-hoc lookup more catalog entries first time we execute a query). This flag only loads the system catalog tables (but not the user objects) which should keep memory low, while loading all often used objects. Still user-object will need to be loaded ad-hoc, which can make first-query latency a bit higher (most impactful in multi-region clusters).

##### --ysql_use_relcache_file

{{% tags/wrap %}}


Default: `true`
{{% /tags/wrap %}}

Controls whether to use the PostgreSQL relcache init file, which caches critical system catalog entries. If enabled, each PostgreSQL connection loads only this minimal set of cached entries (except if the relcache init file needs to be re-built, for example, after a DDL invalidates the cache). If disabled, each PostgreSQL connection preloads the catalog cache, which consumes more memory but reduces first query latency.

##### --ysql_yb_toast_catcache_threshold

{{% tags/wrap %}}


Default: `-1` (disabled). Minimum: 128 bytes.
{{% /tags/wrap %}}

Specifies the threshold (in bytes) beyond which catalog tuples will get compressed when they are stored in the PostgreSQL catalog cache. Setting this flag reduces memory usage for certain large objects, including functions and views, in exchange for slower catalog refreshes.

To minimize performance impact when enabling this flag, set it to 2KB or higher.

##### --ysql_yb_enable_invalidation_messages

{{% tags/wrap %}}


Default: `true`
{{% /tags/wrap %}}

Enables YSQL backends to generate and consume invalidation messages incrementally for schema changes. When enabled (true), invalidation messages are propagated via the `pg_yb_invalidation_messages` per-database catalog table. Details of the invalidation messages generated by a DDL are also logged when [ysql_log_min_messages](#ysql-log-min-messages) is set to `DEBUG1` or when `yb_debug_log_catcache_events` is set to true. When disabled, schema changes cause a full catalog cache refresh on existing backends, which can result in a latency and memory spike on existing YSQL backends.

## Performance Tuning

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

For information on how defaults for other memory division flags are set, and how memory is divided among processes when this flag is set, refer to [Memory division defaults](../../configuration/smart-defaults/).

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
Default: `0.85`
{{% /tags/wrap %}}

The default is different if [--use_memory_defaults_optimized_for_ysql](#use-memory-defaults-optimized-for-ysql) is true.

The percentage of available RAM to use for this process if [--memory_limit_hard_bytes](#memory-limit-hard-bytes) is `0`.  The special value `-1000` means to instead use the default value for this flag.  Available RAM excludes memory reserved by the kernel.

This flag does not apply to Kubernetes universes. Memory limits are controlled via Kubernetes resource specifications in the Helm chart, and `--memory_limit_hard_bytes` is automatically set from those limits. See [Memory limits for Kubernetes deployments](../../../deploy/kubernetes/single-zone/oss/helm-chart/#memory-limits-for-kubernetes-deployments) for details.

#### Flags controlling the split of memory within a TServer

These settings control the division of memory available to the TServer process.

##### --db_block_cache_size_bytes

{{% tags/wrap %}}


Default: `-1`
{{% /tags/wrap %}}

Size of the shared RocksDB block cache (in bytes).  A value of `-1` specifies to instead use a percentage of this processes' hard memory limit; see [--db_block_cache_size_percentage](#db-block-cache-size-percentage) for the percentage used.  A value of `-2` disables the block cache.

##### --db_block_cache_size_percentage

{{% tags/wrap %}}


Default: `50`
{{% /tags/wrap %}}

The default is different if [--use_memory_defaults_optimized_for_ysql](#use-memory-defaults-optimized-for-ysql) is true.

Percentage of the process' hard memory limit to use for the shared RocksDB block cache if [--db_block_cache_size_bytes](#db-block-cache-size-bytes) is `-1`.  The special value `-1000` means to instead use the default value for this flag.  The special value `-3` means to use an older default that does not take the amount of RAM into account.

##### --tablet_overhead_size_percentage

{{% tags/wrap %}}


Default: `0`
{{% /tags/wrap %}}

The default is different if [--use_memory_defaults_optimized_for_ysql](#use-memory-defaults-optimized-for-ysql) is true.

Percentage of the process' hard memory limit to use for tablet-related overheads. A value of `0` means no limit.  Must be between `0` and `100` inclusive. Exception: `-1000` specifies to instead use the default value for this flag.

Each tablet replica generally requires 700 MiB of this memory.

### Raft and consistency/timing flags

With the exception of flags that have different defaults between `yb-master` and `yb-tserver` (for example, `--evict_failed_followers`), the values used for Raft-related flags in `yb-tserver` configurations should match those in [`yb-master`](../yb-master/#raft-flags) configurations in a typical deployment.

##### --follower_unavailable_considered_failed_sec

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `900` (15 minutes)
{{% /tags/wrap %}}

The duration, in seconds, after which a follower is considered to be failed because the leader has not received a heartbeat. The follower is then evicted from the configuration and the data is re-replicated elsewhere.

The `--follower_unavailable_considered_failed_sec` value should match the value for [--log_min_seconds_to_retain](#log-min-seconds-to-retain).

##### --evict_failed_followers

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `true`
{{% /tags/wrap %}}

Failed followers will be evicted from the Raft group and the data will be re-replicated.

##### --leader_failure_max_missed_heartbeat_periods

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `6`
{{% /tags/wrap %}}

The maximum heartbeat periods that the leader can fail to heartbeat in before the leader is considered to be failed. The total failure timeout, in milliseconds (ms), is [--raft_heartbeat_interval_ms](#raft-heartbeat-interval-ms) multiplied by `--leader_failure_max_missed_heartbeat_periods`.

For read replica clusters, set the value to `10` in all yb-tserver and yb-master configurations.  Because the data is globally replicated, RPC latencies are higher. Use this flag to increase the failure detection interval in such a higher RPC latency deployment.

##### --leader_lease_duration_ms

{{% tags/wrap %}}


Default: `2000`
{{% /tags/wrap %}}

The leader lease duration, in milliseconds. A leader keeps establishing a new lease or extending the existing one with every consensus update. A new server is not allowed to serve as a leader (that is, serve up-to-date read requests or acknowledge write requests) until a lease of this duration has definitely expired on the old leader's side, or the old leader has explicitly acknowledged the new leader's lease.

This lease allows the leader to safely serve reads for the duration of its lease, even during a network partition. For more information, refer to [Leader leases](../../../architecture/transactions/single-row-transactions/#leader-leases-reading-the-latest-data-in-case-of-a-network-partition).

Leader lease duration should be longer than the heartbeat interval, and less than the multiple of `--leader_failure_max_missed_heartbeat_periods` multiplied by `--raft_heartbeat_interval_ms`.

##### --max_stale_read_bound_time_ms

{{% tags/wrap %}}


Default: `10000` (10 seconds)
{{% /tags/wrap %}}

Specifies the maximum bounded staleness (duration), in milliseconds, before a follower forwards a read request to the leader.

In a geo-distributed cluster, with followers located a long distance from the tablet leader, you can use this setting to increase the maximum bounded staleness.

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


Default: `shared_prefix`
{{% /tags/wrap %}}

Key-value encoding to use for regular data blocks in RocksDB. Possible options: `shared_prefix`, `three_shared_parts`.

Only change this flag to `three_shared_parts` after you migrate the whole cluster to the YugabyteDB version that supports it.

##### --rocksdb_compact_flush_rate_limit_bytes_per_sec

{{% tags/wrap %}}


Default: `1GB` (1 GB/second)
{{% /tags/wrap %}}

Used to control rate of memstore flush and SSTable file compaction.

##### --rocksdb_universal_compaction_min_merge_width

{{% tags/wrap %}}


Default: `4`
{{% /tags/wrap %}}

Compactions run only if there are at least `rocksdb_universal_compaction_min_merge_width` eligible files and their running total (summation of size of files considered so far) is within `rocksdb_universal_compaction_size_ratio` of the next file in consideration to be included into the same compaction.

##### --rocksdb_max_background_compactions

{{% tags/wrap %}}


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


Default: `2GB`
{{% /tags/wrap %}}

Threshold beyond which a compaction is considered large.

##### --rocksdb_level0_file_num_compaction_trigger

{{% tags/wrap %}}


Default: `5`.
{{% /tags/wrap %}}

Number of files to trigger level-0 compaction. Set to `-1` if compaction should not be triggered by number of files at all.

##### --rocksdb_universal_compaction_size_ratio

{{% tags/wrap %}}


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


Default: `256MB` (256 MB/second)
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


Default: `1`
{{% /tags/wrap %}}

The maximum number of threads allowed for non-admin full compactions. This includes post-split compactions (compactions that remove irrelevant data from new tablets after splits) and scheduled full compactions.

##### --full_compaction_pool_max_queue_size

{{% tags/wrap %}}


Default: `500`
{{% /tags/wrap %}}

The maximum number of full compaction tasks that can be queued simultaneously. This includes post-split compactions (compactions that remove irrelevant data from new tablets after splits) and scheduled full compactions.

##### --auto_compact_check_interval_sec

{{% tags/wrap %}}


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


Default: `100`
{{% /tags/wrap %}}

If `enable_wait_queues=true`, this controls the rate at which each tablet's wait queue polls transaction coordinators for the status of transactions which are blocking contentious resources.

### DDL concurrency flags

##### --ysql_enable_db_catalog_version_mode

{{% tags/wrap %}}


Default: `true`
{{% /tags/wrap %}}

Enable the per database catalog version mode. A DDL statement that
affects the current database can only increment catalog version for
that database.

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

##### --enable_heartbeat_pg_catalog_versions_cache

{{% tags/wrap %}}


Default: `false`
{{% /tags/wrap %}}

Whether to enable the use of heartbeat catalog versions cache for the
`pg_yb_catalog_version` table which can help to reduce the number of reads
from the table. This is beneficial when there are many databases and/or
many yb-tservers in the cluster.

Note that `enable_heartbeat_pg_catalog_versions_cache` is only used when [ysql_enable_db_catalog_version_mode](#ysql-enable-db-catalog-version-mode) is true.

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

To learn about the Auto Analyze service, see [Auto Analyze service](../../../additional-features/auto-analyze).

Auto analyze is automatically enabled when the [cost-based optimizer](../../../best-practices-operations/ysql-yb-enable-cbo/) (CBO) is enabled ([yb_enable_cbo](#yb_enable_cbo) is set to `on`).

In v2025.2 and later, CBO and Auto Analyze are enabled by default in new universes when you deploy using yugabyted, YugabyteDB Anywhere, or YugabyteDB Aeon. In addition, when upgrading a deployment to v2025.2 or later, if the universe has the cost-based optimizer enabled (`on`), YugabyteDB will enable Auto Analyze.

To explicitly control the service, you can set the `ysql_enable_auto_analyze` flag.

##### --ysql_enable_auto_analyze

{{% tags/wrap %}}

Default: `false`
{{% /tags/wrap %}}

Enable the Auto Analyze service, which automatically runs ANALYZE to update table statistics for tables that have changed more than a configurable threshold.

In v2025.2 and later, Auto Analyze is enabled by default in new universes when you deploy using yugabyted, YugabyteDB Anywhere, or YugabyteDB Aeon.

##### --ysql_auto_analyze_threshold

{{% tags/wrap %}}

Default: `50`
{{% /tags/wrap %}}

The minimum number of mutations needed to run ANALYZE on a table. For more details, see [Auto Analyze service](../../../additional-features/auto-analyze).

##### --ysql_auto_analyze_scale_factor

{{% tags/wrap %}}

Default: `0.1`
{{% /tags/wrap %}}

The fraction defining when sufficient mutations have been accumulated to run ANALYZE for a table. For more details, see [Auto Analyze service](../../../additional-features/auto-analyze).

##### --ysql_auto_analyze_min_cooldown_per_table

{{% tags/wrap %}}

Default: `10000` (10 seconds)
{{% /tags/wrap %}}

The minimum duration (in milliseconds) for the cooldown period between successive runs of ANALYZE on a specific table by the auto analyze service. For more details, see [Auto Analyze service](../../../additional-features/auto-analyze).

##### --ysql_auto_analyze_max_cooldown_per_table

{{% tags/wrap %}}

Default: `86400000` (24 hours)
{{% /tags/wrap %}}

The maximum duration (in milliseconds) for the cooldown period between successive runs of ANALYZE on a specific table by the auto analyze service. For more details, see [Auto Analyze service](../../../additional-features/auto-analyze).

##### --ysql_auto_analyze_cooldown_per_table_scale_factor

{{% tags/wrap %}}

Default: `2`
{{% /tags/wrap %}}

The exponential factor by which the per table cooldown period is scaled up each time from the value ysql_auto_analyze_min_cooldown_per_table to the value ysql_auto_analyze_max_cooldown_per_table. For more details, see [Auto Analyze service](../../../additional-features/auto-analyze). 

##### --ysql_auto_analyze_batch_size

{{% tags/wrap %}}

Default: `10`
{{% /tags/wrap %}}

The maximum number of tables the Auto Analyze service tries to analyze in a single ANALYZE statement.

##### --ysql_cluster_level_mutation_persist_interval_ms

{{% tags/wrap %}}

Default: `10000`
{{% /tags/wrap %}}

Interval at which the reported node level table mutation counts are persisted to the underlying auto-analyze mutations table.

##### --ysql_cluster_level_mutation_persist_rpc_timeout_ms

{{% tags/wrap %}}

Default: `10000`
{{% /tags/wrap %}}

Timeout for the RPCs used to persist mutation counts in the auto-analyze mutations table.

##### --ysql_node_level_mutation_reporting_interval_ms

{{% tags/wrap %}}

Default: `5000`
{{% /tags/wrap %}}

Interval, in milliseconds, at which the node-level table mutation counts are sent to the Auto Analyze service, which tracks table mutation counts at the cluster level.

##### --ysql_node_level_mutation_reporting_timeout_ms

{{% tags/wrap %}}

Default: `5000`
{{% /tags/wrap %}}

Timeout, in milliseconds, for the node-level mutation reporting RPC to the Auto Analyze service.

##### --ysql_enable_auto_analyze_service

{{% tags/wrap %}}
{{<tags/feature/deprecated>}}
{{<tags/feature/t-server>}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

Enable the Auto Analyze service, which automatically runs ANALYZE to update table statistics for tables that have changed more than a configurable threshold.

##### --ysql_enable_table_mutation_counter

{{% tags/wrap %}}
{{<tags/feature/deprecated>}}

Default: `false`
{{% /tags/wrap %}}

Enable per table mutation (INSERT, UPDATE, DELETE) counting. The Auto Analyze service runs ANALYZE when the number of mutations of a table exceeds the threshold determined by the [ysql_auto_analyze_threshold](#ysql-auto-analyze-threshold) and [ysql_auto_analyze_scale_factor](#ysql-auto-analyze-scale-factor) settings.

### Advisory lock flags

To learn about advisory locks, see [Advisory locks](../../../architecture/transactions/concurrency-control/#advisory-locks).

##### --ysql_yb_enable_advisory_locks

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
Default: `true`
{{% /tags/wrap %}}

Enables advisory locking.

This value must match on all YB-Master and YB-TServer configurations of a YugabyteDB cluster.

##### --num_advisory_locks_tablets

{{% tags/wrap %}}


Default: `1`
{{% /tags/wrap %}}

Number of tablets used for the advisory locks table. It must be set before ysql_yb_enable_advisory_locks is set to true on the cluster.

### Index backfill flags

##### --ysql_disable_index_backfill

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

Set this flag to `false` to enable online index backfill. When set to `false`, online index builds run while online, without failing other concurrent writes and traffic.

For details on how online index backfill works, see the [Online Index Backfill](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md) design document.

##### --ycql_disable_index_backfill

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `true`
{{% /tags/wrap %}}

Set this flag to `false` to enable online index backfill. When set to `false`, online index builds run while online, without failing other concurrent writes and traffic.

For details on how online index backfill works, see the [Online Index Backfill](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md) design document.

##### --num_concurrent_backfills_allowed

{{% tags/wrap %}}


Default: `-1` (automatic setting)
{{% /tags/wrap %}}

[Online Index Backfill](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md) uses a number of distributed workers to backfill older data from the main table into the index table. This flag sets the number of concurrent index backfill jobs that are allowed to execute on each yb-tserver process. By default, the number of jobs is set automatically as follows:

- When the node has >= 16 cores, it is set to 8 jobs.
- When the node has < 16 cores, it is set to (number of cores) / 2 jobs.

Increasing the number of backfill jobs can allow the index creation to complete faster, however setting it to a higher number can impact foreground workload operations and also increase the chance of failures and retries of backfill jobs if CPU usage becomes too high.

##### --backfill_index_client_rpc_timeout_ms

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `86400000` (1 day)
{{% /tags/wrap %}}

Timeout (in milliseconds) for the backfill stage of a concurrent CREATE INDEX.

##### --backfill_index_timeout_grace_margin_ms

{{% tags/wrap %}}


Default: `-1`, where the system automatically calculates the value to be approximately 1 second.
{{% /tags/wrap %}}

The time to exclude from the YB-Master flag [ysql_index_backfill_rpc_timeout_ms](../yb-master/#ysql-index-backfill-rpc-timeout-ms) in order to return results to YB-Master in the specified deadline. Should be set to at least the amount of time each batch would require, and less than `ysql_index_backfill_rpc_timeout_ms`.

##### --backfill_index_write_batch_size

{{% tags/wrap %}}


Default: `128`
{{% /tags/wrap %}}

The number of table rows to backfill in a single backfill job. In case of [GIN indexes](../../../explore/ysql-language-features/indexes-constraints/gin/), the number can include more index rows. When index creation is slower than expected on large tables, increasing this parameter to 1024 or 2048 may speed up the operation. However, care must be taken to also tune the associated timeouts for larger batch sizes.

### Other performance tuning options

##### --allowed_preview_flags_csv

{{% tags/wrap %}}{{<tags/feature/restart-needed>}}{{% /tags/wrap %}}

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

## Security

### Security flags for encryption and certificates

For details on enabling encryption in transit, see [Encryption in transit](../../../secure/tls-encryption/).

##### --certs_dir

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `""` (Uses `<data drive>/yb-data/tserver/data/certs`.)
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

Allow insecure connections. Set to `false` to prevent any process with unencrypted communication from joining a cluster. Note that this flag requires [`use_node_to_node_encryption`](#use-node-to-node-encryption) to be enabled and [`use_client_to_server_encryption`](#use-client-to-server-encryption) to be enabled.

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


Default: `false`
{{% /tags/wrap %}}

Enable server-server (node-to-node) encryption between YugabyteDB YB-Master and YB-TServer servers in a cluster or universe. To work properly, all YB-Master servers must also have their [--use_node_to_node_encryption](../yb-master/#use-node-to-node-encryption) setting enabled.

When enabled, [--allow_insecure_connections](#allow-insecure-connections) should be set to false to disallow insecure connections.

##### --cipher_list

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `DEFAULTS`
{{% /tags/wrap %}}

Specify cipher lists for TLS 1.2 and below. (For TLS 1.3, use [--ciphersuite](#ciphersuite).) Use a colon (":") separated list of TLSv1.2 cipher names in order of preference. Use an exclamation mark ("!") to exclude ciphers. For example:

```sh
--cipher_list DEFAULTS:!DES:!IDEA:!3DES:!RC2
```

This allows all ciphers for TLS 1.2 to be accepted, except those matching the category of ciphers omitted.

This flag requires a restart or rolling restart.

For more information, refer to [SSL_CTX_set_cipher_list](https://www.openssl.org/docs/man1.1.1/man3/SSL_CTX_set_cipher_list.html) in the OpenSSL documentation.

##### --ciphersuite

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `DEFAULTS`
{{% /tags/wrap %}}

Specify cipher lists for TLS 1.3. (For TLS 1.2 and below, use [--cipher_list](#cipher-list).)

Use a colon (":") separated list of TLSv1.3 ciphersuite names in order of preference. Use an exclamation mark ("!") to exclude ciphers. For example:

```sh
--ciphersuite DEFAULTS:!CHACHA20
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

### YSQL

The following flags support the use of the [YSQL API](../../../api/ysql/):

##### --enable_ysql

{{% tags/wrap %}}
{{<tags/feature/t-server>}}
Default: `true`
{{% /tags/wrap %}}

Enables the YSQL API.

Ensure that `enable_ysql` values in yb-tserver configurations match the values in yb-master configurations.

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

To specify fine-grained access control over who can access the server, use [`--ysql_hba_conf`](#ysql-hba-conf).

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

The configuration parameters for YugabyteDB are the same as for PostgreSQL, with some minor exceptions. Refer to [Configuration parameters](#postgresql-configuration-parameters).

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

Some connections are reserved for superusers. The total number of superuser connections is determined by the `superuser_reserved_connections` [configuration parameter](#postgresql-configuration-parameters). Connections available to non-superusers is equal to `ysql_max_connections` - `superuser_reserved_connections`.

##### --ysql_default_transaction_isolation

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `'read committed'`
{{% /tags/wrap %}}

Specifies the default transaction isolation level.

Valid values: `serializable`, `'repeatable read'`, `'read committed'`, and `'read uncommitted'`.

[Read Committed isolation](../../../explore/transactions/isolation-levels/) is supported only if the YB-TServer flag `yb_enable_read_committed_isolation` is set to `true`.

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
{{<tags/feature/tp>}}
Default: `0`
{{% /tags/wrap %}}

Specifies the maximum size (in bytes) of total data returned in one response when the query layer fetches rows of a table from DocDB. Used to bound how many rows can be returned in one request. Set to 0 to have no size limit.

You can also specify the value as a string. For example, you can set it to `'10MB'`.

You should have at least one of row limit or size limit set.

If both `--ysql_yb_fetch_row_limit` and `--ysql_yb_fetch_size_limit` are greater than zero, then limit is taken as the lower bound of the two values.

See also the [yb_fetch_size_limit](#yb-fetch-size-limit) configuration parameter. If both flag and parameter are set, the parameter takes precedence.

##### --ysql_yb_fetch_row_limit

{{% tags/wrap %}}
{{<tags/feature/tp>}}
Default: `1024`
{{% /tags/wrap %}}

Specifies the maximum number of rows returned in one response when the query layer fetches rows of a table from DocDB. Used to bound how many rows can be returned in one request. Set to 0 to have no row limit.

You should have at least one of row limit or size limit set.

If both `--ysql_yb_fetch_row_limit` and `--ysql_yb_fetch_size_limit` are greater than zero, then limit is taken as the lower bound of the two values.

See also the [yb_fetch_row_limit](#yb-fetch-row-limit) configuration parameter. If both flag and parameter are set, the parameter takes precedence.

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
{{<tags/feature/restart-needed>}}
{{<tags/feature/t-server>}}
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
Default: `262144` (256kB, type: int32)
{{% /tags/wrap %}}

Size of YSQL layer output buffer, in bytes. YSQL buffers query responses in this output buffer until either a buffer flush is requested by the client or the buffer overflows.

As long as no data has been flushed from the buffer, the database can retry queries on retryable errors. For example, you can increase the size of the buffer so that YSQL can retry [read restart errors](../../../architecture/transactions/read-restart-error).

##### --ysql_yb_bnl_batch_size

{{% tags/wrap %}}


Default: `1024`
{{% /tags/wrap %}}

Sets the size of a tuple batch that's taken from the outer side of a [batched nested loop (BNL) join](../../../architecture/query-layer/join-strategies/#batched-nested-loop-join-bnl). When set to 1, BNLs are effectively turned off and won't be considered as a query plan candidate.

See also the [yb_bnl_batch_size](#yb-bnl-batch-size) configuration parameter. If both flag and parameter are set, the parameter takes precedence.

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

### YCQL

The following flags support the use of the [YCQL API](../../../api/ycql/):

##### --use_cassandra_authentication

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

Specify `true` to enable YCQL authentication (`username` and `password`), enable YCQL security statements (`CREATE ROLE`, `DROP ROLE`, `GRANT ROLE`, `REVOKE ROLE`, `GRANT PERMISSION`, and `REVOKE PERMISSION`), and enforce permissions for YCQL statements.

##### --cql_proxy_bind_address

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `0.0.0.0:9042` (`127.0.0.1:9042`)
{{% /tags/wrap %}}

Specifies the bind address for the YCQL API.

##### --cql_proxy_webserver_port

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `12000`
{{% /tags/wrap %}}

Specifies the port for monitoring YCQL metrics.

##### --cql_table_is_transactional_by_default

{{% tags/wrap %}}
{{<tags/feature/restart-needed>}}
Default: `false`
{{% /tags/wrap %}}

Specifies if YCQL tables are created with transactions enabled by default.

##### --ycql_require_drop_privs_for_truncate

{{% tags/wrap %}}


Default: `false`
{{% /tags/wrap %}}

Set this flag to `true` to reject [`TRUNCATE`](../../../api/ycql/dml_truncate) statements unless allowed by [`DROP TABLE`](../../../api/ycql/ddl_drop_table) privileges.

##### --ycql_enable_audit_log

Set this flag to `true` to enable audit logging for the universe.

For details, see [Audit logging for the YCQL API](../../../secure/audit-logging/audit-logging-ycql).

##### --ycql_allow_non_authenticated_password_reset

{{% tags/wrap %}}


Default: `false`
{{% /tags/wrap %}}

Set this flag to `true` to enable a superuser to reset a password.

Note that to enable the password reset feature, you must first set the [`use_cassandra_authentication`](#use-cassandra-authentication) flag to false.

<!--
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
-->
