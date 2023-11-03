---
title: TLS and authentication
headerTitle: TLS and authentication
linkTitle: TLS and authentication
description: Use authentication in conjunction with TLS encryption.
headcontent: Use authentication in conjunction with TLS encryption
image: /images/section_icons/secure/authentication.png
menu:
  v2.12:
    identifier: tls-authentication
    parent: tls-encryption
    weight: 800
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../tls-authentication" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>

TLS can be configured in conjunction with authentication using the following configuration flags related to TLS and authentication:

* [`ysql_enable_auth`](../../authentication/password-authentication/) to enable password (md5) authentication
* [`use_client_to_server_encryption`](../client-to-server/) to enable client-server TLS encryption
* [`ysql_hba_conf_csv`](../../authentication/host-based-authentication/) to manually set a host-based authentication (HBA) configuration

The default (auto-generated) configuration in the `ysql_hba.conf` file depends on whether auth (`ysql_enable_auth`) and/or TLS (`use_client_to_server_encryption`) are enabled.

The four default cases are shown in the following table.

| | Auth disabled | Auth enabled |
---|---|---|
| TLS disabled | `host all all all trust`</br>(no ssl, no password) | `host all all all md5`</br>(no ssl, password required) |
| TLS enabled | `hostssl all all all trust`</br>(require ssl, no password) | `hostssl all all all md5`</br>(require ssl and password) |

{{< note title="Note" >}}
Before version 2.5.2, when TLS was enabled the default was to use the more strict `cert` option when auth was disabled, and `md5 clientcert=1` (effectively md5 + cert) when auth was enabled.
{{< /note >}}

Additionally, `ysql_hba_conf_csv` can be used to manually configure a custom HBA configuration.

For instance, to use TLS with both `md5` and `cert` authentication, you can set the `ysql_hba_conf_csv` flag as follows:

```sh
hostssl all all all md5 clientcert=1
```

The `ysql_hba_conf_csv` rules are added above the auto-generated rules in the `ysql_hba.conf` file, so if they do not match the connection type, database, user, or host, then the auto-generated rules (that is, from the table above) may still be used.

If the custom user-defined rules only apply to some connection types (for example, host vs hostssl), databases, users, or hosts, the auto-generated rules are applied to the non-matching hosts, users, or databases. To fully disable the auto-generated rules, use the `reject` auth option.

For instance, to enable TLS with `cert` authentication, but only for some particular database, user, and host, use the following `ysql_hba_conf_csv` setting:

```sh
hostssl mydb myuser myhost cert,hostssl all all all reject
```

## Examples

To begin, download and configure sample certificates:

```sh
$ wget https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/ent/test_certs/ca.crt
$ wget -O node.127.0.0.1.key https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/ent/test_certs/ysql.key
$ wget -O node.127.0.0.1.crt https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/ent/test_certs/ysql.crt
$ chmod 600 ca.crt node.127.0.0.1.key node.127.0.0.1.crt
$ CERTS=`pwd`
$ ENABLE_TLS="use_client_to_server_encryption=true,certs_for_client_dir=$CERTS"
```

`node.127.0.0.1.crt` and `node.127.0.0.1.key` are the default values for the `ssl_cert_file` and `ssl_key_file` server-side configuration for a YSQL node. If your local IP is not 127.0.0.1, then use the appropriate local IP to name the two files. Alternatively use `ysql_pg_conf` to set `ssl_cert_file` and `ssl_key_file` to the appropriate values.

### TLS without authentication

This configuration requires the client to use client-to-server encryption to connect.

Create the database:

```sh
$ ./bin/yb-ctl destroy && ./bin/yb-ctl create --tserver_flags="$ENABLE_TLS"
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
ysqlsh (11.2-YB-2.7.0.0-b0)
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
Type "help" for help.
```

The default `ysqlsh` SSL mode is `prefer` (see <https://www.postgresql.org/docs/11/libpq-ssl.html>), which tries SSL first, but falls back to no-ssl if the server does not support it.

In this case, a plain `ysqlsh` with no options will work and use encryption:

```sh
$ ./bin/ysqlsh
```

```output
ysqlsh (11.2-YB-2.7.0.0-b0)
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
Type "help" for help.
```

The following examples omit setting `sslmode` for brevity.

### TLS with authentication

This configuration requires the client to use client-to-server encryption and authenticate with a password to connect.

To create the database, execute the following command:

```sh
$ ./bin/yb-ctl destroy && ./bin/yb-ctl create --tserver_flags="$ENABLE_TLS,ysql_enable_auth=true"
```

To connect to the database, the password is required (see second line below):

```sh
$ ./bin/ysqlsh
```

```output
Password for user yugabyte:
ysqlsh (11.2-YB-2.7.0.0-b0)
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
Type "help" for help.
```

The other modes (that is, `sslmode=require` or `disable`) behave analogously.

### TLS with authentication via certificate

This configuration requires the client to use client-to-server encryption and authenticate with the appropriate certificate to connect.

{{< note title="Note" >}}
Before version 2.5.2, this was the default for TLS without authentication. This example shows the `ysql_hba_conf_csv` configuration to use to replicate the previous behavior.
{{< /note >}}

To create the database, execute the following command:

```sh
$ ./bin/yb-ctl destroy && ./bin/yb-ctl create \
    --tserver_flags="$ENABLE_TLS" \
    --ysql_hba_conf_csv="hostssl all all all cert"
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
ysqlsh (11.2-YB-2.7.0.0-b0)
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
Type "help" for help.
```

### TLS with password and certificate authentication

This configuration requires the client to use client-to-server encryption and authenticate with both the appropriate certificate and the password to connect.

{{< note title="Note" >}}
Before version 2.5.2, this was the default for TLS with authentication. This example shows the `ysql_hba_conf_csv` configuration to use to replicate the previous behavior.

{{< /note >}}

To create the database, execute the following command:

```sh
$ ./bin/yb-ctl destroy && ./bin/yb-ctl create \
    --tserver_flags="$ENABLE_TLS,ysql_enable_auth=true" \
    --ysql_hba_conf_csv="hostssl all all all md5 clientcert=1"
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
ysqlsh (11.2-YB-2.7.0.0-b0)
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
Type "help" for help.
```
