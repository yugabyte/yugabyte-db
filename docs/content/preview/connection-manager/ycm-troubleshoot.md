---
title: YSQL Connection Manager Troubleshooting
headerTitle: Troubleshoot
linkTitle: Troubleshoot
description: Troubleshooting YSQL Connection Manager problems
headcontent: Troubleshoot issues with YSQL Connection Manager
menu:
  preview:
    identifier: ycm-troubleshoot
    parent: connection-manager
    weight: 40
type: docs
---

For information on YSQL Connection Manager limitations, refer to [Limitations](../ycm-setup/#limitations).

## Connection exhaustion due to sticky connections

**User-facing behavior**

- Partially exhausted pool: Higher query execution latencies, verify using `queued_logical_connections` and `avg_wait_time_ns` on the 13000/connections endpoint.

- Completely exhausted physical connection pool: Clients appear to hang/timeout while trying to authenticate or execute queries.

**Verify**

Use the 13000/connections endpoint, and search for `sticky_connections`.

Search for "sticky" in Connection Manager logs with `log_debug` enabled.

To offer complete correctness/range of support, connection manager makes connections for some features [sticky by default](./ycm-setup/#sticky-connections). You can verify this using the sticky connections metrics.

Depending on your use case, you can enable flags or workarounds to avoid stickiness.

## Timeouts (connection exhaustion/high multiplexity factor)

**User-facing behavior**

For higher query execution latencies, timeouts may occur based on application layer handling.

**Verify**

Higher latencies will be reflected in the time taken for the logical connection to attach to a physical connection.

Look at `avg_wait_time_ns` on the 13000/connections endpoint; higher values account for higher latencies.

If you have high multiplexity (many more logical connections than physical connections), clients may be waiting too long to attach to a physical connection. Consider increasing `ysql_max_connections`.

## Unsupported authentication methods

- SCRAM. Salted Challenge Response Authentication Mechanism is not currently supported by Connection Manager. SCRAM is a secure method for user authentication where the client and server verify each other's identity without ever transmitting the actual password in plain text. This provides protection against password sniffing on an unreliable network. It's a challenge response mechanism, where the server sends a challenge and the client returns it a calculated value based on its password and salt. As part of the challenge response mechanism, Connection Manager does not throw any challenge to the client and so no response is received from the client.

- TCP IPv6 logical connections are not authenticated with Connection Manager. Connection Manager assumes all logical connections are TCP IPv4 protocol connections. If any IPv6 logical connection tries to connect, Connection Manager will still authenticate against IPv4 in the host column of the hba file.

- CERT authentication. Connection Manager does not support CERT authentication (verify-full/verify-ca). CERT authentication requires connections to be SSL encrypted. Authentication with Connection Manager still happens on the database side. Therefore Connection Manager should forward all client credentials (for example, the password) along with setting up the SSL context on the database while doing authentication. The logical connection presents client certificates to Connection Manager and it's difficult to pass the same certificates to the database to perform authentication. If it were to pass, the physical connections are Unix socket connections (no SSL/Encryption), which makes it difficult to set up a fake SSL context in which client certificates are needed to be processed for the purpose of certificate authentication via Connection Manager. Client certificates are loaded during the initial SSL handshake of the client with the postmaster process without Connection Manager.

## Different SSL behaviour

Although Connection Manager doesn't throw errors and supports all SSL modes that clients can set in a connection, the behaviour could be slightly different. The following scenarios are the corner cases which do not show the same behaviour as YugabyteDB does without Connection Manager. The main reason for the difference in behaviour is sometimes authentication is done at the Connection Manager layer itself, rather than following the standard authentication mechanism (where authentication happens on the server based on credentials forwarded by Connection Manager).

- Enable TLS in cluster, add `{host all all all trust}` in the HBA file, and try making a connection using sslmode=disable. The connection will fail with Connection Manager, whereas it will be successfully created if connected directly to the database port.

- Enable TLS in cluster, add `{host all all all trust}` in the HBA file, and try making a connection using sslmode=allow. An encrypted connection will be created with Connection Manager, whereas when connecting to a database port an unencrypted connection will be created.

- Enable TLS in cluster and create a connection using sslmode=disable. Connection Manager will throw the following error: `odyssey: c8240c445726f: SSL is required`; whereas when connecting to the database port, the error message is `FATAL:  no pg_hba.conf entry for host`.
