---
title: References for using YB Migration Engine
headerTitle: References for using YB Migration Engine
linkTitle: YB Migration Engine
description: Learn about SSL connectivity, manual schema changes and so on while using the YB Migration Engine.
section: REFERENCE
menu:
  preview:
    identifier: yb-migration-reference
    parent: connectors
    weight: 20
isTocNested: true
showAsideToc: true
---


## Migrator machine requirements

The machine where you'll run the yb_migrate command should:

- support CentOS or Ubuntu.
- connect to both the source and the target database.
- have local storage at least 1.5 times the size of the source database.
- have sudo access.

## Export directory

Before starting migration, you should create the export directory on a file system that has enough space to keep the entire data dump. Next, you should provide the path of the export directory as a mandatory argument (`--export-dir`) to each invocation of the yb_migrate command.

The export directory has the following sub-directories and files:

- `reports` directory contains the generated Migration Assessment Report.
- `schema` directory contains the source database schema translated to PostgreSQL. The schema is partitioned into smaller files by the schema object type such as tables, views, and so on.
- `data` directory contains TSV (Tab Separated Values) files that are passed to the COPY command on the target database.
- `metainfo` and `temp` directories are used by yb_migrate for internal bookkeeping.
- `yb_migrate.log` contains log messages.

## Data modeling

Before performing migration from your source database to YugabyteDB,

### Review your sharding strategies

YugabyteDB supports two primary ways of sharding: by HASH and RANGE. The default sharding method is set to HASH as we believe that this is the better option for most OLTP applications. You can read more about why in [Hash and range sharding](../architecture/docdb-sharding/sharding/). When exporting out of a PostgreSQL database, be aware that if you want RANGE partitioning, you must call it out in the schema creation.

For most workloads, it is recommended to use HASH partitioning because it efficiently partitions the data, and spreads it evenly across all nodes.

RANGE partitioning may be advantageous for particular use cases. Consider a use case which is time series specific; here you'll be querying data for specific time buckets. In this case, using RANGE partitioning to split buckets into the specific time buckets will help to improve the speed and efficiency of the query.

Additionally, you can use a combination of HASH and RANGE sharding for your primary key by choosing a HASH value as the partition key, and a RANGE value as the clustering key.

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

{{< note title="Manual schema changes" >}}

- `CREATE INDEX CONCURRENTLY` : This feature is not supported yet in YugabyteDB. You should remove the `CONCURRENTLY` clause before trying to import the schema.

- Include the primary key definition in the `CREATE TABLE` statement. Primary Key cannot be added to a partitioned table using the `ALTER TABLE` statement.

{{< /note >}}

## Unsupported features

Currently, yb_migrate doesn't support the following features:

<!-- | Feature | Description/Alternatives  | Github issue |
| :------ | :---------- | :----------- |
| BLOB and CLOB | yb_migrate currently ignores all columns of type BLOB/CLOB. <br>  Use another mechanism to load the attributes till this feature is supported.| https://github.com/yugabyte/yb-db-migration/issues/43 |
| Tablespaces |  If the source database is PostgreSQL, manually creating Tablespaces in the target Yugabyte database might  work. | https://github.com/yugabyte/yb-db-migration/issues/47 |
| ALTER VIEW | YugabyteDB does not yet support any schemas containing `ALTER VIEW` statements. | https://github.com/yugabyte/yb-db-migration/issues/48 | -->
