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

## Connection exhaustion via sticky connections

**User-facing behavior**

- Partially exhausted pool - Higher query execution latencies, verify using queued_logical_connections and avg_wait_time_ns on 13000/connections endpoint.

- Completely exhausted physical connection pool - Clients appear to hang/timeout while trying to authenticate or execute queries.

**Verification**

Using 13000/connections endpoint - search for sticky_connections.

Search for "sticky" in Connection Manager logs with `log_debug` enabled.

To offer complete correctness/range of support, connection manager usually goes for stickiness by default for some features - should be verified through sticky connection metrics.

Depending on customer use case, flags/workarounds can be enabled to avoid stickiness.

## Timeouts (connection exhaustion/high multiplexity factor)

**User-facing behavior**

Higher query execution latencies, timeouts may occur based on application layer handling.

**Verification**

Higher latencies will be reflected in the time taken for the logical connection to attach to a physical connection.

Look at avg_wait_time_ns on 13000/connections endpoint, higher values here account for higher latencies.

If high multiplexity (logical conns >> phys conns), clients may be waiting too long to attach to a physical connection - consider increasing `ysql_max_connections`.

## Non Supported Authentication Methods/Configs

SCRAM Authentication: SCRAM stands for Salted Challenge Response Authentication Mechanism which is not supported with connection manager. It's a secure method for user authentication where the client and server verify each other's identity without ever transmitting the actual password in plain text. Providing protection against password sniffing on an unreliable network. It's a challenge response mechanism, where the server sends a challenge and the client returns it a calculated value based on its password and salt. As part of the challenge response mechanism, the connection manager does not throw any challenge to the client and hence no response is received from the client.

TCP IPv6 logical connections are not authenticated with the connection manager. The connection manager assumes all logical connections are TCP IPv4 protocol connections while authentication. If any IPv6 logical connection tries to connect, the connection manager will still authenticate against IPv4 in the host column of the hba file.

CERT Authentication: Connection manager does not support CERT authentication (verify-full/verify-ca). CERT authentication requires connection to be SSL encrypted. Authentication with the connection manager still happens on the database side. Therefore the connection manager should forward all client credentials (e.g password) along with setting up the ssl context on the database while doing authentication. The logical connection presents client certificates to the connection manager and it's difficult to pass the same certificates to the database to perform authentication. Even if we manage to pass, the physical connections are unix socket connections (no SSL/Encryption) which makes it difficult to set up a fake ssl context in which client certificates are needed to be processed for the purpose of cert authentication via connection manager. Client certificates are loaded during the initial ssl handshake of the client with the postmaster process without a connection manager.

## Different Behaviour for sslmode

Although we don't throw errors and support all sslmode that clients can set in a connection, the behaviour could be slightly different. Following scenarios are the corner cases which do not show the same behaviour as YugabyteDB does without a connection manager. The main reason for the difference in behaviour is sometimes authentication is done at connection manager layer itself rather than following standard authentication mechanism (where auth happens on the server based on credentials forwarded by connection manager).

Enable TLS in cluster, add `{host all all all trust}` in HBA file. Then try making a connection using sslmode=disable. Connection will fail with conn mgr whereas successfully created if connected directly to the db port.

Enable TLS in cluster, add `{host all all all trust}` in HBA file. Then try making a connection using sslmode=allow. An encrypted connection will be created with conn mgr whereas on connecting with a db port an unencrypted conn will be created.

Enable TLS in cluster and create a conn using sslmode=disable. Error message with conn mgr will be `odyssey: c8240c445726f: SSL is required` whereas on connecting to db port the error message: FATAL:  no pg_hba.conf entry for host.
