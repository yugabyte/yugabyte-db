---
title: Set up YSQL Connection Manager
headerTitle: Set up YSQL Connection Manager
linkTitle: Setup
description: Set up YSQL Connection Manager
headcontent: YSQL Connection Manager flags and settings
menu:
  preview:
    identifier: ycm-setup
    parent: connection-manager
    weight: 10
type: docs
---

## Start YSQL Connection Manager

To start a YugabyteDB cluster with YSQL Connection Manager, set the [yb-tserver](../../../reference/configuration/yb-tserver/) flag `enable_ysql_conn_mgr` to true.

For example, to create a single-node cluster with YSQL Connection Manager using [yugabyted](../../../reference/configuration/yugabyted/), use the following command:

```sh
./bin/yugabyted start --tserver_flags "enable_ysql_conn_mgr=true" --ui false
```

When `enable_ysql_conn_mgr` is set, each YB-TServer starts the YSQL Connection Manager process along with the PostgreSQL process. You should see one YSQL Connection Manager process per YB-TServer.

{{< note >}}

To create a large number of client connections, ensure that "SHMMNI" (the maximum number of concurrent shared memory segments an OS allows) as well as [ulimit](../../../deploy/manual-deployment/system-config/#set-ulimits) is set correctly as follows:

1. Open the file `/etc/sysctl.conf`.
1. Add `kernel.shmmni = 32768` (support for 30000 clients) at the end of the file.
1. To refresh the settings, use `sudo sysctl -p`.
{{< /note >}}

## Configuration

By default, when YSQL Connection Manager is enabled, it uses the port 5433, and the backend database is assigned a random free port.

To explicitly set a port for YSQL, you should specify ports for the flags `ysql_conn_mgr_port` and [ysql_port](../../../reference/configuration/yugabyted/#advanced-flags).

The following table describes YB-TServer flags related to YSQL Connection Manager:

| flag | Description | Default |
|:---- | :---------- | :------ |
| enable_ysql_conn_mgr | Enables YSQL Connection Manager for the cluster. YB-TServer starts a YSQL Connection Manager process as a child process. | false |
| enable_ysql_conn_mgr_stats | Enable statistics collection from YSQL Connection Manager. These statistics are displayed at the endpoint `<ip_address_of_cluster>:13000/connections`. | true |
| ysql_conn_mgr_idle_time | Specifies the maximum idle time (in seconds) allowed for database connections created by YSQL Connection Manager. If a database connection remains idle without serving a client connection for a duration equal to, or exceeding this value, it is automatically closed by YSQL Connection Manager. | 60 |
| ysql_conn_mgr_max_client_connections | Maximum number of concurrent client connections allowed. | 10000 |
| ysql_conn_mgr_min_conns_per_db | Minimum number of physical connections that is present in the pool. This limit is not considered while closing a broken physical connection. | 1 |
| ysql_conn_mgr_num_workers | Number of worker threads used by YSQL Connection Manager. If set to 0, the number of worker threads will be half of the number of CPU cores. | 0 |
| ysql_conn_mgr_stats_interval | Interval (in seconds) for updating the YSQL Connection Manager statistics. | 1 |
| ysql_conn_mgr_superuser_sticky | Make superuser connections sticky. | true |
| ysql_conn_mgr_port | YSQL Connection Manager port to which clients can connect. This must be different from the PostgreSQL port set via `pgsql_proxy_bind_address`. | 5433 |
| ysql_conn_mgr_server_lifetime | The maximum duration (in seconds) that a backend PostgreSQL connection managed by YSQL Connection Manager can remain open after creation. | 3600 |
| ysql_conn_mgr_log_settings | Comma-separated list of log settings for YSQL Connection Manger. Can include  'log_debug', 'log_config', 'log_session', 'log_query', and 'log_stats'. | "" |
| ysql_conn_mgr_use_auth_backend | Enable the use of the auth-backend for authentication of logical connections. When false, the older auth-passthrough implementation is used. | true |
| ysql_conn_mgr_readahead_buffer_size | Size of the per-connection buffer (in bytes) used for IO read-ahead operations in YSQL Connection Manager. | 8192 |
| ysql_conn_mgr_tcp_keepalive | TCP keepalive time (in seconds) in YSQL Connection Manager. Set to zero to disable keepalive. | 15 |
| ysql_conn_mgr_tcp_keepalive_keep_interval | TCP keepalive interval (in seconds) in YSQL Connection Manager. Only applicable if 'ysql_conn_mgr_tcp_keepalive' is enabled. | 75 |
| ysql_conn_mgr_tcp_keepalive_probes | Number of TCP keepalive probes in YSQL Connection Manager. Only applicable if 'ysql_conn_mgr_tcp_keepalive' is enabled. | 9 |
| ysql_conn_mgr_tcp_keepalive_usr_timeout | TCP user timeout (in milliseconds) in YSQL Connection Manager. Only applicable if 'ysql_conn_mgr_tcp_keepalive' is enabled. | 9 |
| ysql_conn_mgr_pool_timeout | Server pool wait timeout (in milliseconds) in YSQL Connection Manager. This is the time clients wait for an available server, after which they are disconnected. If set to zero, clients wait for server connections indefinitely. | 0 |
| ysql_conn_mgr_sequence_support_mode | Sequence support mode when YSQL connection manager is enabled. When set to  'pooled_without_curval_lastval', the currval() and lastval() functions are not supported. When set to 'pooled_with_curval_lastval', the currval() and lastval() functions are supported. For both settings, monotonic sequence order is not guaranteed if `ysql_sequence_cache_method` is set to `connection`. To also support monotonic order, set this flag to `session`. | pooled_without_curval_lastval |
| ysql_conn_mgr_optimized_extended_query_protocol | Enables optimization of [extended-query protocol](https://www.postgresql.org/docs/current/protocol-overview.html#PROTOCOL-QUERY-CONCEPTS) to provide better performance; note that while optimization is enabled, you may have correctness issues if you alter the schema of objects used in prepared statements. If set to false, extended-query protocol handling is always fully correct but unoptimized. | true |

## Sticky connections

YSQL Connection Manager enables a larger number of client connections to efficiently share a smaller pool of backend processes using a many-to-one multiplexing model. However, in certain cases, a backend process may enter a state that prevents connection multiplexing between transactions. When this occurs, the backend process remains dedicated to a single logical connection (hence the term "sticky connection") for the entire session rather than just a single transaction. This behavior deviates from the typical use case, where backend processes are reassigned after each transaction.

Currently, once formed, sticky connections remain sticky until the end of the session. At the end of the session, the backend process corresponding to a sticky connection is destroyed along with the connection, and the connection does not return to the pool.

When using YSQL Connection Manager, sticky connections can form in the following circumstances:

- Creating TEMP tables
- Declaring a CURSOR using the WITH HOLD attribute
- Using a PREPARE query (not to be confused with protocol-level preparation of statements)
- Superuser connections; if you want superuser connections to not be sticky, set the `ysql_conn_mgr_superuser_sticky` flag to false
- Using a SEQUENCE with `ysql_conn_mgr_sequence_support_mode` set to `session`. (Other values for this flag provide lesser support without stickiness.)
- Replication connections
- Setting the following configuration parameters during the session:
  - `session_authorization`
  - `role`
  - `default_tablespace`
  - `temp_tablespaces`
  - Any string-type variables of extensions
  - `yb_read_after_commit_visibility`
