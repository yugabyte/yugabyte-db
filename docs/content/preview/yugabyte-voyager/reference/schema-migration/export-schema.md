---
title: export schema reference
headcontent: yb-voyager export schema
linkTitle: export schema
description: YugabyteDB Voyager export schema reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-export-schema
    parent: schema-migration
    weight: 10
type: docs
---


[Export the schema](../../../migrate/migrate-steps/#export-and-analyze-schema) from the source database.

## Syntax

```text
Usage: yb-voyager export schema [ <arguments> ... ]
```

### Arguments

The valid *arguments* for export schema are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| --comments-on-objects | Enable export of comments associated with database objects. (default: false) |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help for schema. |
| --oracle-cdb-name <name> | Oracle Container Database Name in case you are using a multi-tenant container database. Required for Oracle live migrations only. |
| --oracle-cdb-sid <SID> | Oracle System Identifier (SID) of the Container Database that you wish to use while exporting data from Oracle instances. Required for Oracle live migrations only. |
| --oracle-cdb-tns-alias <alias> | Name of TNS Alias you wish to use to connect to Oracle Container Database in case you are using a multi-tenant container database. Required for Oracle live migrations only. |
| --oracle-db-sid <SID> | Oracle System Identifier you can use while exporting data from Oracle instances. Oracle migrations only.|
| --oracle-home <path> | Path to set `$ORACLE_HOME` environment variable. `tnsnames.ora` is found in `$ORACLE_HOME/network/admin`. Oracle migrations only.|
| [--oracle-tns-alias](../../yb-voyager-cli/#ssl-connectivity) <alias> | TNS (Transparent Network Substrate) alias configured to establish a secure connection with the server. Oracle migrations only. |
| --send-diagnostics | Sends [diagnostics](../../../diagnostics-report/) information to Yugabyte. (default: true)|
| --source-db-host <hostname> | Domain name or IP address of the machine on which the source database server is running. |
| --source-db-name <name> | Source database name. |
| --source-db-password <password>| Source database password. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. Alternatively, you can also specify the password by setting the environment variable `SOURCE_DB_PASSWORD`. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose it in single quotes. |
| --source-db-port <port> | Port number of the source database machine. |
| --source-db-schema <schemaName> | Schema name of the source database. Not applicable for MySQL. |
| --source-db-type <databaseType> | One of `postgresql`, `mysql`, or `oracle`. |
| --source-db-user <username> | Name of the source database user (typically `ybvoyager`). |
| [--source-ssl-cert](../../yb-voyager-cli/#ssl-connectivity) <certificateName> | Name of the certificate which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-key](../../yb-voyager-cli/#ssl-connectivity) <keyName> | Name of the key which is part of the SSL `<cert,key>` pair. |
| [--source-ssl-crl](../../yb-voyager-cli/#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--source-ssl-mode](../../yb-voyager-cli/#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--source-ssl-root-cert](../../yb-voyager-cli/#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| --start-clean | Starts a fresh schema export after clearing the `schema` directory.|
| --use-orafce | Use the Orafce extension. Oracle migrations only. (default: true) |
| --verbose | Display extra information in the output. (default: false) |
| -y, --yes | Answer yes to all prompts during the export schema operation. (default: false) |

### Example

```sh
yb-voyager export schema --export-dir /dir/export-dir \
        --source-db-type oracle \
        --source-db-host 127.0.0.1 \
        --source-db-port 1521 \
        --source-db-user ybvoyager \
        --source-db-password 'password'  \
        --source-db-name source_db \
        --source-db-schema source_schema \
        --start-clean

```
