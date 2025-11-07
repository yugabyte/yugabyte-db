---
title: TLS and authentication
headerTitle: TLS and authentication
linkTitle: TLS and authentication
description: Use authentication in conjunction with TLS encryption.
headcontent: Use authentication in conjunction with TLS encryption
tags:
  other: ysql
menu:
  stable:
    identifier: tls-authentication
    parent: tls-encryption
    weight: 800
type: docs
---

TLS can be configured in conjunction with authentication using the following configuration flags related to TLS and authentication:

* [ysql_enable_auth](../../authentication/password-authentication/) to enable password (md5) authentication
* [use_client_to_server_encryption](../server-to-server/) to enable client-server TLS encryption
* [ysql_hba_conf_csv](../../authentication/host-based-authentication/) to manually set a host-based authentication (HBA) configuration

The default (auto-generated) configuration in the `ysql_hba.conf` file depends on whether auth (`ysql_enable_auth`) and/or TLS (`use_client_to_server_encryption`) are enabled.

The four default cases are shown in the following table.

| | Auth disabled | Auth enabled |
| :--- | :--- | :--- |
| TLS disabled | `host all all all trust`</br>(no ssl, no password) | `host all all all md5`</br>(no ssl, password required) |
| TLS enabled | `hostssl all all all trust`</br>(require ssl, no password) | `hostssl all all all md5`</br>(require ssl and password) |

Additionally, `ysql_hba_conf_csv` can be used to manually configure a custom HBA configuration.

For instance, to use TLS with both password authentication and client certificate verification, you can set the `ysql_hba_conf_csv` flag as follows:

```sh
hostssl all all all md5 clientcert=verify-full
```

{{< note title="Note" >}}
To use the client certificate only for verification (signed by the CA) but not for authentication, you can set `clientcert` to `verify-ca`.
{{< /note >}}

The `ysql_hba_conf_csv` rules are added above the auto-generated rules in the `ysql_hba.conf` file, so if they do not match the connection type, database, user, or host, then the auto-generated rules (that is, from the table above) may still be used.

If the custom user-defined rules only apply to some connection types (for example, host vs hostssl), databases, users, or hosts, the auto-generated rules are applied to the non-matching hosts, users, or databases. To fully disable the auto-generated rules, use the `reject` auth option.

For instance, to enable TLS with `cert` authentication, but only for some particular database, user, and host, use the following `ysql_hba_conf_csv` setting:

```sh
hostssl mydb myuser myhost cert,hostssl all all all reject
```

## Examples

To secure clusters when deploying using yugabyted, you use the [--secure flag](../../../reference/configuration/yugabyted/#start), which enables encryption in transit and authentication. For the purposes of illustration, the following examples enable these features manually.

To begin, generate and configure certificates using the following steps:

1. Generate the certificates and keys for the local IP address (`127.0.0.1` in this example) using the `cert generate_server_certs` command. See [create certificates for a secure local cluster](../../../reference/configuration/yugabyted/#create-certificates-for-a-secure-local-multi-node-cluster) for more information.

    ```sh
    ./bin/yugabyted cert generate_server_certs --hostnames=127.0.0.1
    ```

    Certificates are generated in the `<HOME>/var/generated_certs/<hostname>` directory.

    ```output
    127.0.0.1
    ├── ca.crt
    ├── node.127.0.0.1.crt
    └── node.127.0.0.1.key
    ```

    `node.127.0.0.1.crt` and `node.127.0.0.1.key` are the default values for the `ssl_cert_file` and `ssl_key_file` server-side configuration for a YSQL node. If your local IP is not 127.0.0.1, then use the appropriate local IP to name the two files. Alternatively, use [ysql_pg_conf_csv](../../../reference/configuration/all-flags-yb-tserver/#ysql-pg-conf-csv) to set `ssl_cert_file` and `ssl_key_file` to the appropriate values.

1. Set the `ENABLE_TLS` variable to point to the directory where the certificates are stored, and set the `use_client_to_server_encryption` flag to true as follows:

    ```sh
    $ cd ~/var/generated_certs/127.0.0.1
    $ CERTS=$(pwd)
    $ ENABLE_TLS="use_client_to_server_encryption=true,certs_for_client_dir=$CERTS"
    ```

### TLS without authentication

This configuration requires the client to use client-to-server encryption to connect.

Create the database:

```sh
$ ./bin/yugabyted start \
    --tserver_flags="$ENABLE_TLS"
```

Without SSL enabled in the client, the connection fails.

```sh
$ ./bin/ysqlsh "sslmode=disable"
```

```output
ysqlsh: FATAL:  no pg_hba.conf entry for host "127.0.0.1", user "yugabyte", database "yugabyte", SSL off
```

To connect, SSL must be enabled in the client.

```sh
$ ./bin/ysqlsh "sslmode=require"
```

```output
ysqlsh (15.2-YB-{{<yb-version version="stable">}}-b0)
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
Type "help" for help.
```

The default ysqlsh SSL mode is `prefer` (refer to [SSL Support](https://www.postgresql.org/docs/15/libpq-ssl.html) in the PostgreSQL documentation), which tries SSL first, but falls back to `disable` if the server does not support it.

In this case, a plain ysqlsh with no options will work and use encryption:

```sh
$ ./bin/ysqlsh
```

```output
ysqlsh (15.2-YB-{{<yb-version version="stable">}}-b0)
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
Type "help" for help.
```

The following examples omit setting `sslmode` for brevity.

### TLS with authentication

This configuration requires the client to use client-to-server encryption and authenticate with a password to connect.

To create the database, execute the following command:

```sh
$ ./bin/yugabyted destroy && \
    ./bin/yugabyted cert generate_server_certs --hostnames=127.0.0.1 && \
    ./bin/yugabyted start \
    --tserver_flags="$ENABLE_TLS,ysql_enable_auth=true"
```

To connect to the database, the password is required (see second line below):

```sh
$ ./bin/ysqlsh
```

```output
Password for user yugabyte:
ysqlsh (15.2-YB-{{<yb-version version="stable">}}-b0)
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
Type "help" for help.
```

The other modes (that is, `sslmode=require` or `disable`) behave analogously.

### TLS with SCRAM-based password authentication

This configuration (also known as SCRAM-SHA-256-PLUS) enables authentication with SCRAM using TLS channel-binding as described in [RFC 7677](https://tools.ietf.org/html/rfc7677). When enabled, information about the TLS channel is encoded in the authentication messages exchanged; this ensures the client and server share the same TLS channel (that is, binding the channel to the authentication mechanism).

To enable SCRAM-SHA-256-PLUS for a user already using SCRAM-SHA-256, you set the `ysql_enable_scram_channel_binding` YB-TServer flag to true.

For example, to create a database, execute the following command:

```sh
$ ./bin/yugabyted destroy && \
    ./bin/yugabyted cert generate_server_certs --hostnames=127.0.0.1 && \
    ./bin/yugabyted start \
    --tserver_flags="ysql_enable_auth=true,ysql_enable_scram_channel_binding=true,$ENABLE_TLS"
```

Note that this configuration requires the client to use client-to-server encryption.

For more information on enabling SCRAM-SHA-256 password encryption, refer to [Password authentication](../../../secure/authentication/password-authentication/#scram-sha-256).

### TLS with authentication via certificate

This configuration requires the client to use client-to-server encryption and authenticate with the appropriate certificate to connect.

To create the database, execute the following command:

```sh
$ ./bin/yugabyted destroy && \
    ./bin/yugabyted cert generate_server_certs --hostnames=127.0.0.1 && \
    ./bin/yugabyted start \
    --tserver_flags="$ENABLE_TLS,ysql_hba_conf_csv={hostssl all all all cert}"
```

Without a certificate, the connection fails.

```sh
$ ./bin/ysqlsh
```

```output
ysqlsh: FATAL:  connection requires a valid client certificate
FATAL:  no pg_hba.conf entry for host "127.0.0.1", user "yugabyte", database "yugabyte", SSL off
```

Use the following command to connect with a certificate:

```sh
$ ./bin/ysqlsh "sslcert=$CERTS/node.127.0.0.1.crt sslkey=$CERTS/node.127.0.0.1.key sslrootcert=$CERTS/ca.crt"
```

```output
ysqlsh (15.2-YB-{{<yb-version version="stable">}}-b0)
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
Type "help" for help.
```

### TLS with password and certificate authentication

This configuration requires the client to use client-to-server encryption and authenticate with both the appropriate certificate and the password to connect.

To create the database, execute the following command:

```sh
$ ./bin/yugabyted destroy && \
    ./bin/yugabyted cert generate_server_certs --hostnames=127.0.0.1 && \
    ./bin/yugabyted start \
    --tserver_flags="$ENABLE_TLS,ysql_hba_conf_csv={hostssl all all all md5 clientcert=verify-full}"
```

The `ysql_enable_auth=true` flag is redundant in this case, but included to demonstrate the ability to override the auto-generated configuration using `ysql_hba_conf_csv`.

Without a certificate and password, the connection fails.

```sh
$ ./bin/ysqlsh
```

```output
ysqlsh: FATAL:  connection requires a valid client certificate
FATAL:  no pg_hba.conf entry for host "127.0.0.1", user "yugabyte", database "yugabyte", SSL off
```

Use the following command to connect with a certificate and password:

```sh
$ ./bin/ysqlsh "sslcert=$CERTS/node.127.0.0.1.crt sslkey=$CERTS/node.127.0.0.1.key sslrootcert=$CERTS/ca.crt"
```

```output
Password for user yugabyte:
ysqlsh (15.2-YB-{{<yb-version version="stable">}}-b0)
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
Type "help" for help.
```
