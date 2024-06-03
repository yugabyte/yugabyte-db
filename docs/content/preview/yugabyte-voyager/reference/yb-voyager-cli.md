---
title: yb-voyager CLI
headcontent: yb-voyager command line interface reference
linkTitle: yb-voyager CLI
description: YugabyteDB Voyager CLI and SSL connectivity.
menu:
  preview_yugabyte-voyager:
    identifier: yb-voyager-cli
    parent: reference-voyager
    weight: 100
type: docs
rightNav:
  hideH4: true
---

yb-voyager is a command line executable for migrating databases from PostgreSQL, Oracle, and MySQL to a YugabyteDB database.

## Syntax

```sh
yb-voyager [ <migration-step>... ] [ <arguments> ... ]
```

- *migration-step*: See [Commands](#commands)

- *arguments*: one or more arguments, separated by spaces.

### Command line help

To display the available online help, run:

```sh
yb-voyager --help
```

To display the available online help for any migration step, run:

```sh
yb-voyager [ <migration-step>... ] --help
```

### Version check

To verify the version of yb-voyager installed on your machine, run:

```sh
yb-voyager version
```

## Commands

The list of commands for various phases of migration are as follows:

- [Export schema](../../reference/schema-migration/export-schema/)
- [Analyze schema](../../reference/schema-migration/analyze-schema/)
- [Import schema](../../reference/schema-migration/import-schema/)
- [Export data](../../reference/data-migration/export-data/)
- [Export data status](../../reference/data-migration/export-data/#export-data-status)
- [Get data-migration-report](../../reference/data-migration/export-data/#get-data-migration-report)
- [Export data from target](../../reference/data-migration/export-data/#export-data-from-target)
- [Import data](../../reference/data-migration/import-data/)
- [Import data status](../../reference/data-migration/import-data/#import-data-status)
- [Import data to source](../../reference/data-migration/import-data/#import-data-to-source)
- [Import data to source-replica](../../reference/data-migration/import-data/#import-data-to-source-replica)
- [Import data file](../../reference/bulk-data-load/import-data-file/)
- [Cutover to target](../../reference/cutover-archive/cutover/#cutover-to-target)
- [Cutover to source](../../reference/cutover-archive/cutover/#cutover-to-source)
- [Cutover to source-replica](../../reference/cutover-archive/cutover/#cutover-to-source-replica)
- [Cutover status](../../reference/cutover-archive/cutover/#cutover-status)
- [Archive changes](../../reference/cutover-archive/archive-changes)
- [End migration](../../reference/end-migration)

## SSL Connectivity

You can instruct yb-voyager to connect to the source or target database over an SSL connection. Connecting securely to PostgreSQL, MySQL, and YugabyteDB requires you to pass a similar set of arguments to yb-voyager. Oracle requires a different set of arguments.

### PostgreSQL/MySQL options

The following table summarizes the arguments and options you can pass to yb-voyager to establish an SSL connection for PostgreSQL or MySQL.

 | Arguments | Description |
 | :-------- | :---------- |
 | &#8209;&#8209;source&#8209;ssl&#8209;mode | Value of this argument determines whether an encrypted connection is established between yb-voyager and the database server; and whether the certificate of the database server is verified from a CA. <br /> **Options**<ul><li>disable: Only try a non-SSL connection.</li><li>allow: First try a non-SSL connection; if that fails, try an SSL connection. (Not supported for MySQL.)</li><li> prefer (default): First try an SSL connection; if that fails, try a non-SSL connection.</li><li>require: Only try an SSL connection. If a root CA file is present, verify the certificate in the same way as if verify-ca was specified.</li><li> verify-ca: Only try an SSL connection, and verify that the server certificate is issued by a trusted certificate authority (CA).</li><li>verify-full: Only try an SSL connection, verify that the server certificate is issued by a trusted CA and that the requested server host name matches that in the certificate.</li></ul> |
| --source-ssl-cert <br /> --source-ssl-key | These two arguments specify names of the files containing SSL certificate and key, respectively. The `<cert, key>` pair forms the identity of the client. Note: If using [accelerated data export](../../migrate/migrate-steps/#accelerate-data-export-for-mysql-and-oracle), ensure that the keys are in the PKCS8 standard PEM format. |
| &#8209;&#8209;source&#8209;ssl&#8209;root&#8209;cert | Specifies the path to a file containing SSL certificate authority (CA) certificate(s). If the file exists, the server's certificate will be verified to be signed by one of these authorities.
| --source-ssl-crl | Specifies the path to a file containing the SSL certificate revocation list (CRL). Certificates listed in this file, if it exists, will be rejected while attempting to authenticate the server's certificate. If using [accelerated data export](../../migrate/migrate-steps/#accelerate-data-export-for-mysql-and-oracle), this is not supported. |

### Oracle options

The following table summarizes the arguments and options you can pass to yb-voyager to establish an SSL connection for Oracle:

| Arguments | Description |
| :-------- | :---------- |
| &#8209;&#8209;oracle&#8209;tns&#8209;alias | A TNS (Transparent Network Substrate) alias that is configured to establish a secure connection with the server is passed to yb-voyager. When you pass [--oracle-tns-alias](../schema-migration/export-schema/#arguments), you cannot use any other arguments to connect to your Oracle instance including [--source-db-schema](../schema-migration/export-schema/#arguments) and [--oracle-db-sid](../schema-migration/export-schema/#arguments). Note: By default, the expectation is that the wallet files (.sso, .pk12, and so on) are in the TNS_ADMIN directory (the one containing tnsnames.ora). If the wallet files are in a different directory, ensure that you update the wallet location in the `sqlnet.ora` file. If using [accelerated data export](../../migrate/migrate-steps/#accelerate-data-export-for-mysql-and-oracle), to specify a different wallet location, also create a `ojdbc.properties` file in the TNS_ADMIN directory, and add the following: `oracle.net.wallet_location=(SOURCE=(METHOD=FILE)(METHOD_DATA=(DIRECTORY=/path/to/wallet)))`. |

### YugabyteDB options

The following table summarizes the arguments and options you can pass to yb-voyager to establish an SSL connection for YugabyteDB.

| Arguments | Description |
| :-------- | :---------- |
| --target&#8209;ssl&#8209;mode | Value of this argument determines whether an encrypted connection is established between yb-voyager and the database server; and whether the certificate of the database server is verified from a CA. <br /> **Options**<ul><li>disable: Only try a non-SSL connection.</li><li>allow: First try a non-SSL connection; if that fails, try an SSL connection. (Not supported for MySQL.)</li><li> prefer (default): First try an SSL connection; if that fails, try a non-SSL connection.</li><li>require: Only try an SSL connection. If a root CA file is present, verify the certificate in the same way as if verify-ca was specified.</li><li> verify-ca: Only try an SSL connection, and verify that the server certificate is issued by a trusted certificate authority (CA).</li><li>verify-full: Only try an SSL connection, verify that the server certificate is issued by a trusted CA and that the requested server host name matches that in the certificate.</li></ul>
| --target-ssl-cert <br /> --target-ssl-key | These two arguments specify names of the files containing SSL certificate and key, respectively. The `<cert, key>` pair forms the identity of the client. |
| &#8209;&#8209;target&#8209;ssl&#8209;root&#8209;cert | Specifies the path to a file containing SSL certificate authority (CA) certificate(s). If the file exists, the server's certificate will be verified to be signed by one of these authorities. |
| --target-ssl-crl | Specifies the path to a file containing the SSL certificate revocation list (CRL). Certificates listed in this file, if it exists, will be rejected while attempting to authenticate the server's certificate. |
