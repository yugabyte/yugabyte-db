---
title: References
headerTitle: References
linkTitle: References
description: Overview of the yb_migrate database engine for migrating data and applications from other databases to YugabyteDB.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    identifier: references
    parent: db-migration-engine
    weight: 720
isTocNested: true
showAsideToc: true
---

## SSL Connectivity

You can instruct yb_migrate to connect to the source or target database over an SSL connection. Connecting securely to one of these databases : PostgreSQL, MySQL, and YugabyteDB requires you to pass a similar set of arguments to the yb_migrate tool. Oracle, however, requires a different set of arguments.

The following table summarizes the arguments and options you can pass to the yb_migrate utility to establish an SSL connection.

| Database | Arguments | Description |
| :------- | :------------ | :----------- |
| PostgreSQL <br /> MySQL | `--source-ssl-mode`| Value of this argument determines whether an encrypted connection is established between yb_migrate and the database server; and whether the certificate of the database server is verified from a CA. <br /> **Options**<ul><li>disable: Only try a non-SSL connection.</li><li>allow: First try a non-SSL connection; if that fails, try an SSL connection. (Not supported for MySQL.)</li><li> prefer(default): First try an SSL connection; if that fails, try a non-SSL connection.</li><li>require: Only try an SSL connection. If a root CA file is present, verify the certificate in the same way as if verify-ca was specified.</li><li> verify-ca: Only try an SSL connection, and verify that the server certificate is issued by a trusted certificate authority (CA).</li><li>verify-full: Only try an SSL connection, verify that the server certificate is issued by a trusted CA and that the requested server host name matches that in the certificate.</li></ul> |
| | `--source-ssl-cert` <br /> `--source-ssl-key` | These two arguments specify names of the files containing SSL certificate and key, respectively. The `<cert, key>` pair forms the identity of the client. |
| | `--source-ssl-root-cert` | This parameter specifies the path to a file containing SSL certificate authority (CA) certificate(s). If the file exists, the server's certificate will be verified to be signed by one of these authorities.
| | `--source-ssl-crl` | This parameter specifies the path to a file containing the SSL certificate revocation list (CRL). Certificates listed in this file, if it exists, will be rejected while attempting to authenticate the server's certificate.
| Oracle | `--oracle-tns-alias` | A TNS alias that is configured to establish a secure connection with the server is passed to yb_migrate. When you pass this argument, you don't need to pass the `--source-db-host`, `--source-db-port`, and `--source-db-name` arguments to yb_migrate.|
| YugabyteDB | `--target-ssl-mode` | Value of this argument determines whether an encrypted connection is established between yb_migrate and the database server; and whether the certificate of the database server is verified from a CA. <br /> **Options**<ul><li>disable: Only try a non-SSL connection.</li><li>allow: First try a non-SSL connection; if that fails, try an SSL connection. (Not supported for MySQL.)</li><li> prefer(default): First try an SSL connection; if that fails, try a non-SSL connection.</li><li>require: Only try an SSL connection. If a root CA file is present, verify the certificate in the same way as if verify-ca was specified.</li><li> verify-ca: Only try an SSL connection, and verify that the server certificate is issued by a trusted certificate authority (CA).</li><li>verify-full: Only try an SSL connection, verify that the server certificate is issued by a trusted CA and that the requested server host name matches that in the certificate.</li></ul>
| | `--target-ssl-cert` <br /> `--target-ssl-key` | These two arguments specify names of the files containing SSL certificate and key, respectively. The `<cert, key>` pair forms the identity of the client. |
| | `--target-ssl-root-cert` | This parameter specifies the path to a file containing SSL certificate authority (CA) certificate(s). If the file exists, the server's certificate will be verified to be signed by one of these authorities. |
| | `--target-ssl-crl` | This parameter specifies the path to a file containing the SSL certificate revocation list (CRL). Certificates listed in this file, if it exists, will be rejected while attempting to authenticate the server's certificate. |

<!-- ## PostgreSQL and MySQL

- `--source-ssl-mode (disable|allow|prefer|require|verify-ca|verify-full)`
    Value of this argument determines:
  - whether an encrypted connection is established between yb_migrate and the database server; and
  - whether the certificate of the database server is verified from a CA.

    Possible values and their meaning is given below:
  - `disable`: Only try a non-SSL connection.
  - `allow`: First try a non-SSL connection; if that fails, try an SSL connection. (Not supported for MySQL.)
  - `prefer` (default): First try an SSL connection; if that fails, try a non-SSL connection.
  - `require`: Only try an SSL connection. If a root CA file is present, verify the certificate in the same way as if verify-ca was specified.
  - `verify-ca`: Only try an SSL connection, and verify that the server certificate is issued by a trusted certificate authority (CA).
  - `verify-full`: Only try an SSL connection, verify that the server certificate is issued by a trusted CA and that the requested server host name matches that in the certificate.

- `--source-ssl-cert` and `--source-ssl-key`

    These two arguments specify names of the files containing SSL certificate and key, respectively. The `<cert, key>` pair forms the identity of the client.

- `--source-ssl-root-cert`

    This parameter specifies the path to a file containing SSL certificate authority (CA) certificate(s). If the file exists, the server's certificate will be verified to be signed by one of these authorities.

- `--source-ssl-crl`

    This parameter specifies the path to a file containing the SSL certificate revocation list (CRL). Certificates listed in this file, if it exists, will be rejected while attempting to authenticate the server's certificate.

## YugabyteDB

You need to pass following arguments to yb_migrate to establish an SSL connection with YugabyteDB:

- `--target-ssl-mode`
- `--target-ssl-cert`
- `--target-ssl-key`
- `--target-ssl-root-cert`
- `--target-ssl-crl`

Semantics of these arguments match with the similarly named arguments described in the previous section.

## Oracle

For Oracle, create a TNS alias that is configured to establish a secure connection with the server. You must then pass the TNS alias to yb_migrate as `--oracle-tns-alias` argument. yb_migrate uses the TNS alias to securely connect to the server.

When you pass the `--oracle-tns-alias` argument, you don't need to pass the `--source-db-host`, `--source-db-port`, and `--source-db-name` arguments to the yb_migrate. -->

{{< note title="Manual schema changes" >}}

- `CREATE INDEX CONCURRENTLY` : This feature is not supported yet in YugabyteDB. You should remove the `CONCURRENTLY` clause before trying to import the schema.

- Include the primary key definition in the `CREATE TABLE` statement. Primary Key cannot be added to a partitioned table using the `ALTER TABLE` statement.

{{< /note >}}
