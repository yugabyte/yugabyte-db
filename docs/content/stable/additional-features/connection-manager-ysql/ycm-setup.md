---
title: Set up YSQL Connection Manager
headerTitle: Set up YSQL Connection Manager
linkTitle: Setup
description: Set up YSQL Connection Manager
headcontent: YSQL Connection Manager flags and settings
menu:
  stable:
    identifier: ycm-setup
    parent: connection-manager
    weight: 10
type: docs
rightNav:
  hideH3: true
  hideH4: true
---

## Start YSQL Connection Manager

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#yugabyted" class="nav-link active" id="yugabyted-tab" data-bs-toggle="tab" role="tab" aria-controls="yugabyted" aria-selected="true">
      <img src="/icons/database.svg" alt="Server Icon">
      Local
    </a>
  </li>
  <li>
    <a href="#platform" class="nav-link" id="platform-tab" data-bs-toggle="tab" role="tab" aria-controls="platform" aria-selected="false">
      <img src="/icons/server.svg" alt="Server Icon">
      YugabyteDB Anywhere
    </a>
  </li>
  <li>
    <a href="#aeon" class="nav-link" id="aeon-tab" data-bs-toggle="tab" role="tab" aria-controls="aeon" aria-selected="false">
      <img src="/icons/cloud.svg" alt="Cloud Icon">
      YugabyteDB Aeon
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="yugabyted" class="tab-pane fade show active" role="tabpanel" aria-labelledby="yugabyted-tab">

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

  </div>
  <div id="platform" class="tab-pane fade" role="tabpanel" aria-labelledby="platform-tab">

{{<tags/feature/ea idea="1368">}}While in Early Access, YSQL Connection Manager is not available in YugabyteDB Anywhere by default. To make connection pooling available, set the **Allow users to enable or disable connection pooling** Global Runtime Configuration option (config key `yb.universe.allow_connection_pooling`) to true. Refer to [Manage runtime configuration settings](../../../yugabyte-platform/administer-yugabyte-platform/manage-runtime-config/). You must be a Super Admin to set global runtime configuration flags.

To enable built-in connection pooling for universes deployed using YugabyteDB Anywhere:

- Turn on the **Connection pooling** option when creating a universe. Refer to [Create a multi-zone universe](../../../yugabyte-platform/create-deployments/create-universe-multi-zone/#advanced-configuration).
- Edit connection pooling on an existing universe. Refer to [Edit connection pooling](../../../yugabyte-platform/manage-deployments/edit-universe/#edit-connection-pooling).

Note that when managing universes using YugabyteDB Anywhere, do not set connection pooling flags, `enable_ysql_conn_mgr`, `ysql_conn_mgr_port`, and `pgsql_proxy_bind_address`.

**Connect**

To connect to YSQL Connection Manager, use the [ysqlsh](../../../api/ysqlsh/) command with the [`-h <IP>`](../../../api/ysqlsh/#h-hostname-host-hostname) flag, instead of specifying the Unix-domain socket directory.

Using the socket directory along with [`-p`](../../../api/ysqlsh/#p-port-port-port) (custom PostgreSQL port or default 6433) will connect you to the PostgreSQL process, not the YSQL Connection Manager process.

  </div>
  <div id="aeon" class="tab-pane fade" role="tabpanel" aria-labelledby="aeon-tab">

{{<tags/feature/ea idea="1368">}}You can enable built-in connection pooling on YugabyteDB Aeon clusters in the following ways:

- When [creating a cluster](/stable/yugabyte-cloud/cloud-basics/create-clusters/), turn on the **Connection Pooling** option. (Connection Pooling is enabled by default for [Sandbox clusters](/stable/yugabyte-cloud/cloud-basics/create-clusters/create-clusters-free/).)
- For clusters that are already created, navigate to the cluster **Settings>Connection Pooling** tab.

Enabling connection pooling on an Aeon cluster gives 10 client connections for every server connection by default.

  </div>
</div>

## Configure

By default, when YSQL Connection Manager is enabled, it uses the port 5433, and the backend database is assigned a random free port.

To explicitly set a port for YSQL, you should specify ports for the flags `ysql_conn_mgr_port` and [ysql_port](../../../reference/configuration/yugabyted/#advanced-flags).

The following table describes YB-TServer flags related to YSQL Connection Manager:

| flag | Description |
| :---- | :---------- |
| enable_ysql_conn_mgr | Enables YSQL Connection Manager for the cluster. YB-TServer starts a YSQL Connection Manager process as a child process.<br>Default: false |
| enable_ysql_conn_mgr_stats | Enable statistics collection from YSQL Connection Manager. These statistics are displayed at the endpoint `<ip_address_of_cluster>:13000/connections`. <br>Default: true |
| ysql_conn_mgr_idle_time | Specifies the maximum idle time (in seconds) allowed for database connections created by YSQL Connection Manager. If a database connection remains idle without serving a client connection for a duration equal to, or exceeding this value, it is automatically closed by YSQL Connection Manager.<br>Default: 60 |
| ysql_conn_mgr_max_client_connections | Maximum number of concurrent client connections allowed.<br>Default: 10000 |
| ysql_conn_mgr_min_conns_per_db | Minimum number of server connections that is present in the pool. This limit is not considered while closing a broken server connection.<br>Default: 1 |
| ysql_conn_mgr_num_workers | Number of worker threads used by YSQL Connection Manager. If set to 0, the number of worker threads will be half of the number of CPU cores.<br>Default: 0 |
| ysql_conn_mgr_stats_interval | Interval (in seconds) for updating YSQL Connection Manager statistics.<br>Default: 1 |
| ysql_conn_mgr_superuser_sticky | Make superuser connections sticky.<br>Default: true |
| ysql_conn_mgr_port | YSQL Connection Manager port to which clients can connect. This must be different from the PostgreSQL port set via `pgsql_proxy_bind_address`.<br>Default: 5433 |
| ysql_conn_mgr_server_lifetime | The maximum duration (in seconds) that a backend PostgreSQL connection managed by YSQL Connection Manager can remain open after creation.<br>Default: 3600 |
| ysql_conn_mgr_log_settings | Comma-separated list of log settings for YSQL Connection Manger. Can include 'log_debug', 'log_config', 'log_session', 'log_query', and 'log_stats'.<br>Default: "" |
| ysql_conn_mgr_use_auth_backend | When set to true ("auth backend mode"), each incoming authentication request is handled by a freshly spawned postgres backend. Set this parameter to false to use an authentication passthrough mechanism, where backends from a pool of "control backends" are reused for all authentication requests; this can potentially reduce new connection acquisition latency. Contact {{% support-general %}} about whether setting this parameter to false is recommended for your workload.<br>Default: true |
| ysql_conn_mgr_readahead_buffer_size | Size of the per-connection buffer (in bytes) used for IO read-ahead operations in YSQL Connection Manager.<br>Default: 8192 |
| ysql_conn_mgr_tcp_keepalive | TCP keepalive time (in seconds) in YSQL Connection Manager. Set to zero to disable keepalive.<br>Default: 15 |
| ysql_conn_mgr_tcp_keepalive_keep_interval | TCP keepalive interval (in seconds) in YSQL Connection Manager. Only applicable if 'ysql_conn_mgr_tcp_keepalive' is enabled.<br>Default: 75 |
| ysql_conn_mgr_tcp_keepalive_probes | Number of TCP keepalive probes in YSQL Connection Manager. Only applicable if `ysql_conn_mgr_tcp_keepalive` is enabled.<br>Default: 9 |
| ysql_conn_mgr_tcp_keepalive_usr_timeout | TCP user timeout (in milliseconds) in YSQL Connection Manager. Only applicable if 'ysql_conn_mgr_tcp_keepalive' is enabled.<br>Default: 9 |
| ysql_conn_mgr_pool_timeout | Server pool wait timeout (in milliseconds) in YSQL Connection Manager. This is the time clients wait for an available server, after which they are disconnected. If set to zero, clients wait for server connections indefinitely.<br>Default: 0 |
| ysql_conn_mgr_sequence_support_mode | Sequence support mode when YSQL Connection Manager is enabled. When set to `pooled_without_curval_lastval`, the currval() and lastval() functions are not supported. When set to `pooled_with_curval_lastval`, the currval() and lastval() functions are supported. For both settings, monotonic sequence order is not guaranteed if `ysql_sequence_cache_method` is set to `connection`. To also support monotonic order, set this flag to `session`.<br>Default: pooled_without_curval_lastval |
| ysql_conn_mgr_optimized_extended_query_protocol | Enables optimization of [extended-query protocol](https://www.postgresql.org/docs/current/protocol-overview.html#PROTOCOL-QUERY-CONCEPTS) to provide better performance; note that while optimization is enabled, you may have correctness issues if you alter the schema of objects used in prepared statements. If set to false, extended-query protocol handling is always fully correct but unoptimized.<br>Default: true |
| ysql_conn_mgr_optimized_session_parameters | Optimize usage of session parameters in YSQL Connection Manager. If set to false, all applied session parameters are replayed at transaction boundaries for each client connection.<br>Default: true |
| ysql_conn_mgr_max_phy_conn_percent | Deprecated in v2025.2.1.0. Use `ysql_conn_mgr_reserve_internal_conns` instead.<br>Maximum percentage of `ysql_max_connections` that YSQL Connection Manager can use for its server connections. A value of 85 establishes a soft limit of 0.85 * `ysql_max_connections` on server connections.<br>Default: 85 |
| ysql_conn_mgr_reserve_internal_conns | The number of physical connections to reserve for internal operations, out of the total number of connections (per node) as set using [ysql_max_connections](../../../reference/configuration/yb-tserver/#ysql-max-connections). The reserved connections bypass YSQL Connection Manager; the remaining connections are available for the Connection Manager pool. For example, if `ysql_max_connections` is 300 and this flag is set to 15, YSQL Connection Manager will have a physical connection limit of 285 (300 - 15) per node.<br>Default: 15. v2025.2.1.0 and later only. |

## Authentication methods

The following table outlines the various authentication methods supported by YugabyteDB and their compatibility with YSQL Connection Manager when a connection matches an HBA ([Host-Based Authentication](../../../secure/authentication/host-based-authentication/)) record.

| | Auth Method | Description |
|:--| :---------------------| :------------ | :---- |
| {{<icon/no>}} | Ident Authentication | Server contacts client's OS to verify username that initiated connection, trusting OS-level identity.|
| {{<icon/no>}} | Peer Authentication | For local/Unix socket connections, server checks that the connecting UNIX user matches the requested database user, relying on OS user identity. |
| {{<icon/yes>}} | Plain/Clear Text Password | Standard password-based authentication, though storing passwords in plain text is not recommended. |
| {{<icon/yes>}} | JWT Authentication (OIDC) | Uses JSON Web Tokens (JWT) from an external Identity Provider (IDP) to securely transmit authentication and authorization information. |
| {{<icon/yes>}} | LDAP Authentication | Verifies users against a centralized directory service using Lightweight Directory Access Protocol (LDAP). |
| {{<icon/no>}} | GSS API or Kerberos| Enables Kerberos-based authentication through a standardized API, allowing secure, enterprise-grade Single Sign-On (SSO) logins without passwords. <br> **Note**: Testing of this feature with YugabyteDB is currently limited. |
| {{<icon/yes>}} | SCRAM-SHA-256  | A secure password-based authentication that protects credentials using hashing, salting, and challenge-response. |
| {{<icon/no>}} | SCRAM-SHA-256-PLUS  | A variant of SCRAM-SHA-256 over TLS channels that performs TLS channel-binding as part of authentication. |
| {{<icon/yes>}} | MD5 | Password-based authentication where the user's password is by default stored in MD5 encryption format in the database. |
| {{<icon/partial>}} | Cert  | Currently, Connection Manager does not support the HBA [auth-method](../../../secure/authentication/host-based-authentication/#auth-method) `cert`, where the server authenticates users via the client certificate (for example, CN/DN mapping). Client-side `sslmode` (such as verify-ca, verify-full), where the client verifies the *server* certificate, is a different layer and is supported (see [SSL modes and encryption in transit](#ssl-modes-and-encryption-in-transit)). |

## SSL modes and encryption in transit

Client connection behavior and server-side policy are handled separately as follows:

- **SSL mode** (client-side connection behavior): controls whether the client uses TLS and how it verifies the server (disable, allow, prefer, require, verify-ca, verify-full). Connection Manager supports all of these client SSL modes.
- **ysql_hba.conf** ([host-based authentication](../../../secure/authentication/host-based-authentication/)): controls client authentication for YSQL connections, including whether connections must use SSL/TLS, whether the client must present a certificate, and whether the server verifies the client's identity using that certificate. Currently, Connection Manager does not support HBA certificate authentication.

### Encryption in transit

YSQL Connection Manager can be enabled universes with or without encryption in transit enabled. Connection Manager automatically determines which mode is required and updates the configuration file depending on whether or not encryption is enabled in the cluster. You can review the default settings in the [template configuration file](https://github.com/yugabyte/yugabyte-db/blob/2025.2.2/src/yb/yql/ysql_conn_mgr_wrapper/ysql_conn_mgr.template.conf).

To enable encrypted connections (TLS/SSL) between your client application and YugabyteDB via YSQL Connection Manager, enable client-to-server encryption in transit on your universe by configuring the following [yb-tserver](../../../reference/configuration/yb-tserver/) flags:

- Set [use_client_to_server_encryption](../../../reference/configuration/yb-tserver/#use-client-to-server-encryption) to true.
- Set [certs_for_client_dir](../../../reference/configuration/yb-tserver/#certs-for-client-dir) to the path containing the server TLS certificates.

For more information on encryption in transit in YugabyteDB, refer to [Encryption in transit](../../../secure/tls-encryption/).

#### Disabled

When encryption in transit is not enabled, TLS is not enabled in the Connection Manager configuration file either.

The default HBA configuration allows unencrypted connections as follows:

```text
# This is an autogenerated file, do not edit manually!
# Internal configuration:
# local all postgres yb-tserver-key
host all all all trust
local all yugabyte trust
```

The following table describes how each client SSL mode behaves when connecting via YSQL Connection Manager with the default HBA configuration and encryption in transit disabled.

| Client SSL mode | Description | Same as PostgreSQL? | Connection succeeds? |
| :-- | :-- | :-- | :-- |
| disable | Client attempts a non-SSL connection | Yes. Unencrypted connection is established. | Yes |
| allow | Tries unencrypted connection first, then secure (encrypted) | Yes. Unencrypted connection is established. | Yes (first non-SSL attempt succeeds) |
| prefer (default in PostgreSQL) | Tries secure connection first, then unencrypted | Yes. Unencrypted connection is established. | Partially. First SSL attempt is rejected by Connection Manager, second non-SSL attempt follows Connection Manager authentication. |
| require | Uses secure connection, fails if not available | Yes. If connection fails, PostgreSQL or Connection Manager sends 'N' (not supported) message to the client; client reports: "server does not support SSL, but SSL was required" | No. Client's SSL attempt is rejected by Connection Manager. |
| verify-ca | Behaves like require. Additionally, verifies server cert against CA, or fails if no valid matching CA certificates are found. | Yes. If connection fails, PostgreSQL or Connection Manager sends 'N' (not supported) message to the client; client reports: "server does not support SSL, but SSL was required" | No. Client's SSL attempt is rejected by Connection Manager. |
| verify-full | Behaves like verify-ca. Additionally, verifies that the server cert matches the host. | Yes. If connection fails, PostgreSQL or Connection Manager sends 'N' (not supported) message to the client; client reports: "server does not support SSL, but SSL was required" | No. Client's SSL attempt is rejected by Connection Manager. |

Adding entries to the default configuration does not change behavior for any of these client SSL modes.

#### Enabled

When encryption in transit is enabled for the cluster (by setting the `use_client_to_server_encryption` and `certs_for_client_dir` flags), Connection Manager automatically switches to require mode (that is, it starts with Odyssey `tls "require"` by reading the TLS-related flags and overwriting the Connection Manager configuration file).

The default HBA file in this mode is as follows:

```text
# This is an autogenerated file, do not edit manually!
# Internal configuration:
# local all postgres yb-tserver-key
hostssl all all all trust
local all yugabyte trust
```

The following table describes how each client SSL mode behaves when connecting via YSQL Connection Manager with the default HBA configuration and encryption in transit enabled.

| Client SSL mode | Description | Same as PostgreSQL? | Connection succeeds? |
| :-- | :-- | :-- | :-- |
| disable | Client attempts a non-SSL connection. | Mostly. Error messages differ. If you add `host all all all trust` to the HBA file, connection fails with Connection Manager, but succeeds with PostgreSQL. | No. Non-SSL attempt is rejected by Connection Manager. |
| allow | Tries unencrypted connection first, then secure | Mostly. Encrypted connection is established. But if you add `host all all all trust` to the HBA file, an encrypted connection is created with Connection Manager, and unencrypted with PostgreSQL. | Partially. First non-SSL attempt is rejected by Connection Manager; second SSL attempt follows Connection Manager authentication. |
| prefer (default in PostgreSQL) | Tries secure connection first, then unencrypted. | Yes. Encrypted connection is established. | Yes. First SSL attempt establishes connection with Connection Manager. |
| require | Uses secure connection, fails if not available. | Yes. Encrypted connection is established. | Yes. An SSL attempt establishes connection with Connection Manager. |
| verify-ca | Behaves like require. Additionally, verifies server cert against CA, or fails if no valid matching CA certificates are found. | Yes. Encrypted connection is established and client verifies the TLS certificate. | Yes. An SSL attempt establishes connection with Connection Manager. |
| verify-full | Behaves like verify-ca. Additionally, verifies that the server cert matches the host. | Yes. Encrypted connection is established and client verifies the TLS certificate and hostname. The handshake happens as follows:<ul><li>Client-side validation: The client checks if the server's certificate is signed by a trusted CA.</li><li>Identity match: The client ensures the certificate's hostname matches the actual server address.</li><li>Connection failure on mismatch: If either check fails, the connection is rejected (for example, JDBC and ysqlsh perform this verification).</li></ul> | Yes. An SSL attempt establishes connection with Connection Manager. |

## Sticky connections

YSQL Connection Manager enables a larger number of client connections to efficiently share a smaller pool of backend processes using a many-to-one multiplexing model. However, in certain cases, a backend process may enter a state that prevents connection multiplexing between transactions. When this occurs, the backend process remains dedicated to a single client connection (hence the term "sticky connection") for the entire session rather than just a single transaction. This behavior deviates from the typical use case, where backend processes are reassigned after each transaction.

Currently, once formed, sticky connections remain sticky until the end of the session. At the end of the session, the backend process corresponding to a sticky connection is destroyed along with the connection, and the connection does not return to the pool.

When using YSQL Connection Manager, sticky connections can form in the following circumstances:

- Creating TEMP tables.
- Declaring a CURSOR using the WITH HOLD attribute.
- Using a PREPARE query (not to be confused with protocol-level preparation of statements).
- Superuser connections; if you want superuser connections to not be sticky, set the `ysql_conn_mgr_superuser_sticky` flag to false.
- Using a SEQUENCE with `ysql_conn_mgr_sequence_support_mode` set to `session`. (Other values for this flag provide lesser support without stickiness.)
- Replication connections.
- Setting the following configuration parameters during the session:
  - `session_authorization`
  - `role`
  - `default_tablespace`
  - `temp_tablespaces`
  - Any string-type variables of extensions
  - `yb_read_after_commit_visibility`

## Limitations

- Changes to [configuration parameters](../../../reference/configuration/yb-tserver/#postgresql-configuration-parameters) for a user or database that are set using ALTER ROLE SET or ALTER DATABASE SET queries may reflect in other pre-existing active sessions.
- YSQL Connection Manager can route up to 10,000 connection pools. This includes pools corresponding to dropped users and databases.
- Prepared statements may be visible to other sessions in the same connection pool. {{<issue 24652>}}
- Attempting to use DEALLOCATE/DEALLOCATE ALL queries can result in unexpected behavior. {{<issue 24653>}}
- Currently, you can't apply custom configurations to individual pools. The YSQL Connection Manager configuration applies to all pools.
- When YSQL Connection Manager is enabled, the backend PID stored using JDBC drivers may not be accurate. This does not affect backend-specific functionalities (for example, cancel queries), but this PID should not be used to identify the backend process.
- By default, `currval` and `nextval` functions do not work when YSQL Connection Manager is enabled. They can be supported with the help of the `ysql_conn_mgr_sequence_support_mode` flag.
- YSQL Connection Manager does not yet support IPv6 connections. {{<issue 24765>}}
- Currently, [auth-method](../../../secure/authentication/host-based-authentication/#auth-method) `cert` is not supported for host-based authentication. {{<issue 20658>}}
- The use of auth-backends (`ysql_conn_mgr_use_auth_backend=true`) to authenticate client connections can result in higher connection acquisition latencies. Contact {{% support-general %}} about whether setting `ysql_conn_mgr_use_auth_backend` to false is the recommended setting for your workload. {{<issue 25313>}}
- Unix socket connections to YSQL Connection Manager are not supported. {{<issue 20048>}}
- Connection manager is not supported or included in the MacOS YugabyteDB releases, and there are currently no plans to support it.
