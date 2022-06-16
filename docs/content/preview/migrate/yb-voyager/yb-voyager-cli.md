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

yb-voyager is a command line executable for migrating databases from PostgreSQL, Oracle, and MySQL to a YugabyteDB database.

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

Specifies the source database type (postrgresql, mysql or oracle).

#### --source-db-host

Specifies the host name of the machine on which the source database server is running.

#### --source-db-user

Specifies the username of the source database.

#### --source-db-password

Specifies the password of the source database.

#### --source-db-name

Specifies the name of the source database.

#### --source-db-schema

Specifies the schema of the source database. Only applicable for Oracle.

#### --output-format

Specifies the format in which the report file is generated. It can be in `html`, `txt`, `json` or `xml`.

#### --target-db-host

Specifies the domain name or IP address of the machine on which target database server is running.

#### --target-db-user

Specifies the username of the target database.

#### --target-db-password

Specifies the password of the target database.

#### --target-db-name

Specifies the name of the target database.

#### –-data-dir

The path to the directory containing the data files to import.

#### --file-table-map

Comma-separated mapping between the file in [data-dir](#–-data-dir) to the corresponding table in the database.
Default : The file name in `table1_data.sql` format where `table1` name will be used as the table name.
Example : `filename1:tablename1,filename2:tablename2[,...]`

#### --delimiter

Default: “\t” (tab); can be changed to comma(,), pipe(|) or any other character.

#### –-has-header

This argument is to be specified only for csv file type.
Default: false; change to true if the csv file contains column names as a header.

## SSL Connectivity

You can instruct yb-voyager to connect to the source or target database over an SSL connection. Connecting securely to PostgreSQL, MySQL, and YugabyteDB requires you to pass a similar set of arguments to yb-voyager. Oracle requires a different set of arguments.

The following table summarizes the arguments and options you can pass to yb-voyager to establish an SSL connection.

| Database | Arguments | Description |
| :------- | :------------ | :----------- |
| PostgreSQL <br /> MySQL | `--source-ssl-mode`| Value of this argument determines whether an encrypted connection is established between yb-voyager and the database server; and whether the certificate of the database server is verified from a CA. <br /> **Options**<ul><li>disable: Only try a non-SSL connection.</li><li>allow: First try a non-SSL connection; if that fails, try an SSL connection. (Not supported for MySQL.)</li><li> prefer (default): First try an SSL connection; if that fails, try a non-SSL connection.</li><li>require: Only try an SSL connection. If a root CA file is present, verify the certificate in the same way as if verify-ca was specified.</li><li> verify-ca: Only try an SSL connection, and verify that the server certificate is issued by a trusted certificate authority (CA).</li><li>verify-full: Only try an SSL connection, verify that the server certificate is issued by a trusted CA and that the requested server host name matches that in the certificate.</li></ul> |
| | `--source-ssl-cert` <br /> `--source-ssl-key` | These two arguments specify names of the files containing SSL certificate and key, respectively. The `<cert, key>` pair forms the identity of the client. |
| | `--source-ssl-root-cert` | Specifies the path to a file containing SSL certificate authority (CA) certificate(s). If the file exists, the server's certificate will be verified to be signed by one of these authorities.
| | `--source-ssl-crl` | Specifies the path to a file containing the SSL certificate revocation list (CRL). Certificates listed in this file, if it exists, will be rejected while attempting to authenticate the server's certificate.
| Oracle | `--oracle-tns-alias` | A TNS alias that is configured to establish a secure connection with the server is passed to yb-voyager. When you pass this argument, you don't need to pass the `--source-db-host`, `--source-db-port`, and `--source-db-name` arguments to yb-voyager.|
| YugabyteDB | `--target-ssl-mode` | Value of this argument determines whether an encrypted connection is established between yb-voyager and the database server; and whether the certificate of the database server is verified from a CA. <br /> **Options**<ul><li>disable: Only try a non-SSL connection.</li><li>allow: First try a non-SSL connection; if that fails, try an SSL connection. (Not supported for MySQL.)</li><li> prefer (default): First try an SSL connection; if that fails, try a non-SSL connection.</li><li>require: Only try an SSL connection. If a root CA file is present, verify the certificate in the same way as if verify-ca was specified.</li><li> verify-ca: Only try an SSL connection, and verify that the server certificate is issued by a trusted certificate authority (CA).</li><li>verify-full: Only try an SSL connection, verify that the server certificate is issued by a trusted CA and that the requested server host name matches that in the certificate.</li></ul>
| | `--target-ssl-cert` <br /> `--target-ssl-key` | These two arguments specify names of the files containing SSL certificate and key, respectively. The `<cert, key>` pair forms the identity of the client. |
| | `--target-ssl-root-cert` | Specifies the path to a file containing SSL certificate authority (CA) certificate(s). If the file exists, the server's certificate will be verified to be signed by one of these authorities. |
| | `--target-ssl-crl` | Specifies the path to a file containing the SSL certificate revocation list (CRL). Certificates listed in this file, if it exists, will be rejected while attempting to authenticate the server's certificate. |
