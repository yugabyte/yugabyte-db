---
title: import data file reference
headcontent: yb-voyager import data file
linkTitle: import data file
description: YugabyteDB Voyager import data file reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-import-data-file
    parent: voyager-import
    weight: 70
type: docs
---

Load data from files in CSV or text format directly to the YugabyteDB database. These data files can be located either on a local filesystem, an AWS S3 bucket, GCS bucket, or an Azure blob. For more details, see [Bulk data load from files](../../migrate/bulk-data-load/).

#### Syntax

```text
Usage: yb-voyager import data file [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for import data file are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| [--batch-size](#batch-size) <number> | Size of batches generated for ingestion during import data. |
| [--data-dir](#data-dir) <path> | Path to the location of the data files to import; this can be a local directory or a URL for a cloud storage location. |
| [--delimiter](#delimiter) | Character used as delimiter in rows of the table(s). Default: comma (,) for CSV file format and tab (\t) for TEXT file format. |
| [--disable-pb](#disable-pb) | Hide progress bars. |
| [--escape-char](#escape-char) | Escape character (default double quotes `"`) only applicable to CSV file format. |
| [--file-opts](#file-opts) <string> | **[Deprecated]** Comma-separated string options for CSV file format. |
| [--null-string](#null-string) | String that represents null value in the data file. |
| [--file-table-map](#file-table-map) <filename1:tablename1> | Comma-separated mapping between the files in [data-dir](#data-dir) to the corresponding table in the database. Multiple files can be imported in one table; for example, `foo1.csv:foo,foo2.csv:foo` or `foo*.csv:foo`. |
| [--format](#format) <format> | One of `CSV` or `text` format of the data file. |
| [--has-header](#has-header) | Applies only to CSV file type. |
| [-e, --export-dir](#export-dir) <path> | Path to the export directory. This directory is a workspace used to keep the exported schema, data, state, and logs.|
| [-h, --help](#command-line-help) | Command line help. |
| [--parallel-jobs](#parallel-jobs) <connectionCount> | Number of parallel COPY commands issued to the target database. |
| [--quote-char](#quote-char) | Character used to quote the values (default double quotes `"`) only applicable to CSV file format. |
| [--send-diagnostics](#send-diagnostics) | Send diagnostics information to Yugabyte. |
| [--start-clean](#start-clean) | Starts a fresh import with data files present in the `data` directory and if any table on YugabyteDB database is non-empty, it prompts whether you want to continue the import without truncating those tables; if yes, then yb-voyager starts ingesting the data present in the data files with upsert mode and for the cases where a table doesn't have a primary key, it may duplicate the data. In that case, use `--exclude-table-list` flag to exclude such tables or truncate those tables manually before using the `start-clean` flag. |
| [--target-db-host](#target-db-host) <hostname> | Hostname of the target database server. |
| [--target-db-name](#target-db-name) <name> | Target database name. |
| [--target-db-password](#target-db-password) <password>| Target database password. |
| [--target-db-port](#target-db-port) <port> | Port number of the target database machine. |
| [--target-db-schema](#target-db-schema) <schemaName> | Schema name of the target database. |
| [--target-db-user](#target-db-user) <username> | Username of the target database. |
| [--target-ssl-cert](#ssl-connectivity) <certificateName> | Name of the certificate which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-key](#ssl-connectivity) <keyName> | Name of the key which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-crl](#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--target-ssl-mode](#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--target-ssl-root-cert](#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| [--verbose](#verbose) | Display extra information in the output. |
| [-y, --yes](#yes) | Answer yes to all prompts during the export schema operation. |

<!-- To do : document the following arguments with description
| --continue-on-error |
| --enable-upsert |
| --target-endpoints |
| --use-public-ip | -->

#### Example

```sh
yb-voyager import data file --export-dir /path/to/yb/export/dir \
        --target-db-host hostname \
        --target-db-port port \
        --target-db-user username \
        --target-db-password password \ # Enclose the password in single quotes if it contains special characters.
        --target-db-name dbname \
        --target-db-schema schemaName \ # MySQL and Oracle only
        --data-dir "/path/to/files/dir/" \
        --file-table-map "filename1:table1,filename2:table2" \
        --delimiter "|" \
        --has-header \
        --file-opts "escape_char=\",quote_char=\"" \
        --format format
```
