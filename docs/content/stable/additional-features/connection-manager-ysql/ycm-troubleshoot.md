---
title: YSQL Connection Manager Troubleshooting
headerTitle: Troubleshoot
linkTitle: Troubleshoot
description: Troubleshooting YSQL Connection Manager problems
headcontent: Troubleshoot issues with YSQL Connection Manager
menu:
  stable:
    identifier: ycm-troubleshoot
    parent: connection-manager
    weight: 60
type: docs
---

For information on YSQL Connection Manager limitations, refer to [Limitations](../ycm-setup/#limitations).

## Connection exhaustion due to sticky connections

**Symptoms**

- Partially exhausted pool: Higher query execution latencies, reflected in higher `queued_logical_connections` and `avg_wait_time_ns` metrics.
- Completely exhausted server connection pool: Clients appear to hang or timeout while trying to authenticate or execute queries.

**Verify**

To offer complete correctness/range of support, Connection Manager makes connections for some features [sticky by default](../ycm-setup/#sticky-connections). You can verify this using the [sticky_connections metric](../ycm-monitor/#metrics). Use the `13000/connections` endpoint and search for `sticky_connections`.

Search for "sticky" in Connection Manager logs with `log_debug` enabled. See [Logging](../ycm-monitor/#logging).

Depending on your use case, you can enable flags or workarounds to avoid stickiness.

## Timeouts (connection exhaustion/high multiplexity factor)

**Symptoms**

For higher query execution latencies, timeouts may occur based on application layer handling.

**Verify**

Higher latencies will be reflected in the time taken for the client connection to attach to a server connection.

Look at `avg_wait_time_ns` on the `13000/connections` endpoint; higher values account for higher latencies.

If you have high multiplexity (many more client connections than server connections), clients may be waiting too long to attach to a server connection. Consider increasing the [ysql_max_connections setting](../ycm-setup/#configure).

{{< note title="Note" >}}

A consistently high `avg_wait_time_ns` metric for the `control_connection` pool indicates that the connection acquisition (authentication) latency induced by the workload might be high. Consider setting `ysql_conn_mgr_use_auth_backend=false`. Contact {{% support-general %}} about the suitability of this setting for your workload.

{{</note>}}

## Unsupported authentication methods

- TCP IPv6 client connections. Connection Manager assumes all client connections use IPv4. If any IPv6 client connection tries to connect, Connection Manager will still authenticate against IPv4 in the host column of the HBA file.

- [Host-based authentication](../../../secure/authentication/host-based-authentication) (HBA). Connection Manager does not support the _server-side_ HBA auth-method `cert`, where the server authenticates users via the client's certificate (CN/DN mapping). Supporting server-side cert-based authentication would require forwarding client certificates to the database over Unix socket backend connections, which is not yet implemented. YSQL Connection Manager does support client SSL modes `verify-ca` and `verify-full`, where the _client_ verifies the _server's_ certificate. For more information, refer to [Authentication methods](../ycm-setup/#authentication-methods).

## SSL behaviour

Although Connection Manager supports all SSL modes that clients can set in a connection, the behaviour can be slightly different. The following corner cases can result in different behavior when using Connection Manager compared to a direct database connection:

- Enable TLS in cluster, add `{host all all all trust}` in the HBA file, and try making a connection using sslmode=disable. The connection will fail with Connection Manager, whereas it will be successfully created if connected directly to the database port.

- Enable TLS in cluster, add `{host all all all trust}` in the HBA file, and try making a connection using sslmode=allow. An encrypted connection will be created with Connection Manager, whereas when connecting to a database port an unencrypted connection will be created.

- Enable TLS in cluster and create a connection using sslmode=disable. Connection Manager will throw the following error: `odyssey: c8240c445726f: SSL is required`; whereas when connecting to the database port, the error message is `FATAL:  no pg_hba.conf entry for host`.

The main reason for these differences in behaviour is because sometimes authentication is done at the Connection Manager layer itself, rather than following the standard authentication mechanism (where authentication happens on the server based on credentials forwarded by Connection Manager).

For details on how each client SSL mode (disable, allow, prefer, require, verify-ca, verify-full) behaves with Connection Manager when TLS is enabled or disabled in the cluster, see [Client SSL modes and TLS](../ycm-setup/#client-ssl-modes-and-tls).
