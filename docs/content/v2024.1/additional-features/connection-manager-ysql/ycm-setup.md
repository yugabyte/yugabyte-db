---
title: Set up YSQL Connection Manager
headerTitle: Set up YSQL Connection Manager
linkTitle: Setup
description: Set up YSQL Connection Manager
headcontent: YSQL Connection Manager flags and settings
menu:
  v2024.1:
    identifier: ycm-setup
    parent: connection-manager
    weight: 10
type: docs
---

## Start YSQL Connection Manager

To start a YugabyteDB cluster with YSQL Connection Manager, set the [yb-tserver](../../../reference/configuration/yb-tserver/) flag `enable_ysql_conn_mgr` to true.

When `enable_ysql_conn_mgr` is set, each YB-TServer starts the YSQL Connection Manager process along with the PostgreSQL process. You should see one YSQL Connection Manager process per YB-TServer.

Because `enable_ysql_conn_mgr` is a preview flag, to use it, add the flag to the [allowed_preview_flags_csv](../../../reference/configuration/yb-tserver/#allowed-preview-flags-csv) list (that is, `allowed_preview_flags_csv=enable_ysql_conn_mgr`).

For example, to create a single-node cluster with YSQL Connection Manager using [yugabyted](../../../reference/configuration/yugabyted/), use the following  command:

```sh
./bin/yugabyted start --tserver_flags "enable_ysql_conn_mgr=true,allowed_preview_flags_csv={enable_ysql_conn_mgr}" --ui false
```

{{< note >}}

To create a large number of client connections, ensure that "SHMMNI" (the maximum number of concurrent shared memory segments an OS allows) as well as [ulimit](../../../deploy/manual-deployment/system-config/#set-ulimits) is set correctly as follows:

1. Open the file `/etc/sysctl.conf`.
1. Add `kernel.shmmni = 32768` (support for 30000 clients) at the end of the file.
1. To refresh the settings, use `sudo sysctl -p`.
{{< /note >}}

## Configure

By default, when YSQL Connection Manager is enabled, it uses the port 5433, and the backend database is assigned a random free port.

To explicitly set a port for YSQL, you should specify ports for the flags `ysql_conn_mgr_port` and [ysql_port](../../../reference/configuration/yugabyted/#advanced-flags).

The following table describes YB-TServer flags related to YSQL Connection Manager:

| flag | Description | Default |
| :---- | :---------- | :------ |
| enable_ysql_conn_mgr | Enables YSQL Connection Manager for the cluster. YB-TServer starts a YSQL Connection Manager process as a child process. | false |
| ysql_conn_mgr_idle_time | Specifies the maximum idle time (in seconds) allowed for database connections created by YSQL Connection Manager. If a database connection remains idle without serving a client connection for a duration equal to, or exceeding this value, it is automatically closed by YSQL Connection Manager. | 60 |
| ysql_conn_mgr_max_client_connections | Maximum number of concurrent database connections YSQL Connection Manager can create per pool. | 10000 |
| ysql_conn_mgr_min_conns_per_db | Minimum number of physical connections that is present in the pool. This limit is not considered while closing a broken physical connection. | 1 |
| ysql_conn_mgr_num_workers | Number of worker threads used by YSQL Connection Manager. If set to 0, the number of worker threads will be half of the number of CPU cores. | 0 |
| ysql_conn_mgr_stats_interval | Interval (in seconds) for updating the YSQL Connection Manager statistics. | 10 |
| ysql_conn_mgr_warmup_db | Database for which warmup needs to be done. | yugabyte |
| enable_ysql_conn_mgr_stats | Enable statistics collection from YSQL Connection Manager. These statistics are displayed at the endpoint `<ip_address_of_cluster>:13000/connections`. | true |
| ysql_conn_mgr_port | YSQL Connection Manager port to which clients can connect. This must be different from the PostgreSQL port set via `pgsql_proxy_bind_address`. | 5433 |
| ysql_conn_mgr_dowarmup | Enable pre-creation of server connections in YSQL Connection Manager. If set to false, the server connections are created lazily (on-demand) in YSQL Connection Manager. | false |

## Authentication methods

The following table outlines the various authentication methods supported by YugabyteDB and their compatibility with the YSQL Connection Manager when a connection matches an HBA (Host-Based Authentication) record.

| | Auth Method | Description |
|:--| :---------------------| :------------ | :---- |
| {{<icon/no>}} | Ident Authentication | Server contacts client's OS to verify username that initiated connection, trusting OS-level identity.|
| {{<icon/no>}} | Peer Authentication | For local/Unix socket connections, server checks that the connecting UNIX user matches the requested database user, relying on OS user identity. |
| {{<icon/yes>}} | Plain/Clear Text Password | Standard password-based authentication, though storing passwords in plain text is not recommended. |
| {{<icon/yes>}} | JWT Authentication (OIDC) | Uses JSON Web Tokens (JWT) from an external Identity Provider (IDP) to securely transmit authentication and authorization information. |
| {{<icon/yes>}} | LDAP Authentication | Verifies users against a centralized directory service using Lightweight Directory Access Protocol (LDAP). |
| {{<icon/no>}} | GSS API or Kerberos| Enables Kerberos-based authentication through a standardized API, allowing secure, enterprise-grade Single Sign-On (SSO) logins without passwords. <br> **Note**: Testing of this feature with YugabyteDB is currently limited.|
| {{<icon/no>}} | SCRAM-SHA-256  | A secure password-based authentication that protects credentials using hashing, salting, and challenge-response. |
| {{<icon/no>}} | SCRAM-SHA-256-PLUS  | A variant of SCRAM-SHA-256 over TLS channels that performs TLS channel-binding as part of authentication. |
| {{<icon/yes>}} | MD5 | Password-based authentication where the user's password is by default stored in MD5 encryption format in the database. |
| {{<icon/no>}} | Cert  | Certificate-based authentication requires the client to provide certificates to the server over a TLS connection for authentication. |

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
- Although the use of auth-backends (`ysql_conn_mgr_use_auth_backend=true`) to authenticate client connections can result in higher connection acquisition latencies, using auth-passthrough (`ysql_conn_mgr_use_auth_backend=false`) may not be suitable depending on your workload. Contact {{% support-general %}} before setting `ysql_conn_mgr_use_auth_backend` to false. {{<issue 25313>}}
- Salted Challenge Response Authentication Mechanism ([SCRAM](../../../secure/authentication/password-authentication/#scram-sha-256)) is not supported with YSQL Connection Manager. {{<issue 25870>}}
- Unix socket connections to YSQL Connection Manager are not supported. {{<issue 20048>}}
- Connection manager is not supported or included in the MacOS YugabyteDB releases, and there are currently no plans to support it.
