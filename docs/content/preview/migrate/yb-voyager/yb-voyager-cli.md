---
title: YugabyteDB Voyager CLI
linkTitle: YugabyteDB Voyager CLI
description: YugabyteDB Voyager CLI and SSL connectivity.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    identifier: yb-voyager-cli
    parent: yb-voyager
    weight: 105
isTocNested: true
showAsideToc: true
---

yb-voyager is a command line executable program that supports migrating databases from PostgreSQL, Oracle, and MySQL to a YugabyteDB database.

## Syntax

```sh
yb_voyager [ <migration-phase>... ] [ <arguments> ... ]
```

- *migration-phase*: See [Migration phase](#migration-phase)
- *arguments*: See [Arguments](#arguments)

### Command line help

To display the available online help, run:

```sh
yb_voyager --help
```

### Migration phase

The following command line options specify the migration phases.

- [export schema](/preview/migrate/yb-voyager/perform-migration/#export-schema)

- [analyze-schema](/preview/migrate/yb-voyager/perform-migration/#analyze-schema)

- [export data](/preview/migrate/yb-voyager/perform-migration/#export-data)

- [import schema](/preview/migrate/yb-voyager/perform-migration/#import-schema)

- [import data](/preview/migrate/yb-voyager/perform-migration/#import-data)

- [import data file](/preview/migrate/yb-voyager/perform-migration/#import-data-file)

### Arguments

#### --export-dir

Specifies the path to the directory containing the data files to export.

#### --source-db-type

Specifies the source database type (PostrgreSQL, Mysql or Oracle).

#### --source-db-host

Specifies the host name of the machine on which the source database server is running.

#### --source-db-user

Specifies the username of the source database.

#### --source-db-password

Specifies the password of the source database.

#### --source-db-name

Specifies the name of the source database.

#### --source-db-schema

Specifies the schema of the source database.

#### --output-format

Specifies the format in which the report file is generated. It can be in `html`, `txt`, `json` or `xml`.

#### --target-db-type

Specifies the target YugabyteDB database between [YugabyteDB](https://www.yugabyte.com/yugabytedb/), [YugabyteDB Anywhere](https://www.yugabyte.com/anywhere/) or [YugabyteDB Managed](https://www.yugabyte.com/managed/).

#### --target-db-host

Specifies the host name of the machine on which target database server is running.

#### --target-db-user

Specifies the username of the source database.

#### --target-db-password

Specifies the password of the source database.

#### --target-db-name

Specifies the name of the source database.

#### –-data-dir

The path to the directory containing the data files to import.

#### --file-table-map

Comma-separated mapping between the file in [data-dir](#–-data-dir) to the corresponding table in the database.
Default : The file name in `table1_data.sql` format where `table1` name will be used as the table name.

#### --delimiter

Default: “\t” (tab); can be changed to comma(,), pipe(|) or any other chanracter.

#### –-has-header

This argument is to be specified only for csv file type.
Default: false, Change to true in case the csv file contains column names as a header.

## SSL Connectivity

You can instruct yb-voyager to connect to the source or target database over an SSL connection. Connecting securely to one of these databases : PostgreSQL, MySQL, and YugabyteDB requires you to pass a similar set of arguments to the yb-voyager tool. Oracle, however, requires a different set of arguments.

The following table summarizes the arguments and options you can pass to the yb-voyager utility to establish an SSL connection.

| Database | Arguments | Description |
| :------- | :------------ | :----------- |
| PostgreSQL <br /> MySQL | `--source-ssl-mode`| Value of this argument determines whether an encrypted connection is established between yb-voyager and the database server; and whether the certificate of the database server is verified from a CA. <br /> **Options**<ul><li>disable: Only try a non-SSL connection.</li><li>allow: First try a non-SSL connection; if that fails, try an SSL connection. (Not supported for MySQL.)</li><li> prefer(default): First try an SSL connection; if that fails, try a non-SSL connection.</li><li>require: Only try an SSL connection. If a root CA file is present, verify the certificate in the same way as if verify-ca was specified.</li><li> verify-ca: Only try an SSL connection, and verify that the server certificate is issued by a trusted certificate authority (CA).</li><li>verify-full: Only try an SSL connection, verify that the server certificate is issued by a trusted CA and that the requested server host name matches that in the certificate.</li></ul> |
| | `--source-ssl-cert` <br /> `--source-ssl-key` | These two arguments specify names of the files containing SSL certificate and key, respectively. The `<cert, key>` pair forms the identity of the client. |
| | `--source-ssl-root-cert` | This parameter specifies the path to a file containing SSL certificate authority (CA) certificate(s). If the file exists, the server's certificate will be verified to be signed by one of these authorities.
| | `--source-ssl-crl` | This parameter specifies the path to a file containing the SSL certificate revocation list (CRL). Certificates listed in this file, if it exists, will be rejected while attempting to authenticate the server's certificate.
| Oracle | `--oracle-tns-alias` | A TNS alias that is configured to establish a secure connection with the server is passed to yb-voyager. When you pass this argument, you don't need to pass the `--source-db-host`, `--source-db-port`, and `--source-db-name` arguments to yb-voyager.|
| YugabyteDB | `--target-ssl-mode` | Value of this argument determines whether an encrypted connection is established between yb-voyager and the database server; and whether the certificate of the database server is verified from a CA. <br /> **Options**<ul><li>disable: Only try a non-SSL connection.</li><li>allow: First try a non-SSL connection; if that fails, try an SSL connection. (Not supported for MySQL.)</li><li> prefer(default): First try an SSL connection; if that fails, try a non-SSL connection.</li><li>require: Only try an SSL connection. If a root CA file is present, verify the certificate in the same way as if verify-ca was specified.</li><li> verify-ca: Only try an SSL connection, and verify that the server certificate is issued by a trusted certificate authority (CA).</li><li>verify-full: Only try an SSL connection, verify that the server certificate is issued by a trusted CA and that the requested server host name matches that in the certificate.</li></ul>
| | `--target-ssl-cert` <br /> `--target-ssl-key` | These two arguments specify names of the files containing SSL certificate and key, respectively. The `<cert, key>` pair forms the identity of the client. |
| | `--target-ssl-root-cert` | This parameter specifies the path to a file containing SSL certificate authority (CA) certificate(s). If the file exists, the server's certificate will be verified to be signed by one of these authorities. |
| | `--target-ssl-crl` | This parameter specifies the path to a file containing the SSL certificate revocation list (CRL). Certificates listed in this file, if it exists, will be rejected while attempting to authenticate the server's certificate. |

{{< note title="Manual schema changes" >}}

- `CREATE INDEX CONCURRENTLY` : This feature is not supported yet in YugabyteDB. You should remove the `CONCURRENTLY` clause before trying to import the schema.

- Include the primary key definition in the `CREATE TABLE` statement. Primary Key cannot be added to a partitioned table using the `ALTER TABLE` statement.

{{< /note >}}
