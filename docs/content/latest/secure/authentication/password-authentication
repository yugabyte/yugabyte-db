---
title: Password authentication in YugabyteDB
headerTitle: Password authentication
linkTitle: Password authentication
description: Use password authentication to improve YugyabyteDB security.
menu:
  latest:
    identifier: password-authentication
    parent: authentication
    weight: 731
isTocNested: true
showAsideToc: true
---

By default, password authentication is disabled, allowing users and clients to connect to and interact with YugabyteDB with minimal effort. For production clusters, password authentication is important for maximizing the security. The password authentication methods work similarly, but differ in how user passwords are stored on the server and how the password provided by the client is sent across the connection.

## Password authentication methods

The following password authentication methods are supported by YugabyteDB.

### `md5`

The MD5 method (`md5`) prevents password sniffing and avoids storing passwords on the server in plain text, but provides no protection if an attacker manages to steal the password hash from the server. This method is the default password encryption for YugabyteDB clusters.

The MD5 hash algorithm is not considered secure against determined attacks. Some of the security risks include:

- If someone has access to a valid username/password combination, or their MD5-styled hash, they can log into any cluster where that user exists with the same username and password.
- The "shared secret" is effectively shared over the wire every time the MD5 authentication method is used.

### `scram-sha-256`

The SCRAM-SHA-256 method (`scram-sh-256`) performs SCRAM-SHA-256 authentication, as described in [RFC 7677](https://tools.ietf.org/html/rfc7677). This challenge-response scheme prevents password sniffing on untrusted connections and supports storing passwords on YugabyteDB clusters in the most secure cryptographically hashed form available. This is the most secure password authentication available and is supported by most of the [client drivers for the YSQL API](../../../reference/drivers/ysql-client-drivers).

Allows for two parties to verify they both know a secret without exchanging the secret.

For additional security, the SCRAM-SHA-256 method can be used with [encryption in transit (TLS encryption)](../../../secure/tls-encryption).

## YugabyteDB database passwords

YugabyteDB database passwords are separate from operating system passwords. The password for each database user is stored in the `pg_authid` system catalog.

Database passwords can be managed using the following:

- YSQL API: [CREATE ROLE](../../../api/ysql/commands/dcl_create_role) and [ALTER ROLE](../../../api/ysql/commands/dcl_alter_role)
- ysqlsh metacommand: [`\password`](../../../admin/ysqlsh/#password-username)

## Configure SCRAM-SHA-256 authentication

To configure a YugabyteDB cluster to use SCRAM-SHA-256 authentication for databases, follow these steps.

1. Change the password encryption method

To change the password encryption method to SCRAM-SHA-256, add the YB-TServer [`--ysql_pg_conf`](../../../reference/configuration/yb-tserver/#ysql-pg-conf) flag and set the value to `scram-sha-256`:

```sh
--ysql_pg_conf="password_encryption=scram-sha-256"
```

or in the `tserver.conf`, add the following line:

```
--ysql_pg_conf=password_encryption=scram-sha-256
```

2. Add the YB-TServer [`--ysql_pg_conf`](../../../reference/configuration/yb-tserver/#ysql-pg-conf) flag, specifying rules for the use of the `scram-sha-256` authentication method.

Here's an example of using the flag to create rules for using SCRAM-SHA-256 authentication:

```
--ysql_hba_conf=host all yugabyte 0.0.0.0/0 trust, host all all 0.0.0.0/0 scram-sha-256, host all yugabyte ::0/0 trust, host all all ::0/0 scram-sha-256
```
??? Need to review this with @ddorian and others

or in the `yb-tserver.conf`, add the following line:

```
???
```

For details on using the [--ysql_hba_conf](../../../reference/configuration/yb-tserver/#ysql-hba-conf) flag to specify client authentication, see [Fine-grained authentication](../../authentication/client-authentication).

## Migrating MD5 passwords to SCRAM

By default, YugabyteDB database passwords are created using the MD5 hash algorithm. If the password authentication is changed to use SCRAM-SHA-256, then all new passwords are created using SCRAM-SHA-256. Existing passwords, created when MD5 was used, can only be used with MD5 passwords.

- Even though the password encryption specified for the cluster is no longer MD5, a user can still connect to the cluster because the password hash did not change for that user. When the password is changed for that user or role, the new password will be hashed using SCRAM-SHA-256.
- In the `ysql_hba_conf` file that specifies [fine-grained authentication](), you can include multiple rules such that the first rule allows the use of MD5 with one group of users and the second rule specifies that all other users use SCRAM-SHA-256. 



## Using TLS with SCRAM



## The pg_authid system catalog

```postgresql
SELECT rolname, rolpassword FROM pg_auth_id;
```

This requires a privileged user, but if someone has that privilege, ...

The psql `\password` metacommand can be used to...

```postgresql
$ \password
```
