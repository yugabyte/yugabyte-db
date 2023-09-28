---
title: import data file reference
headcontent: yb-voyager import data file
linkTitle: import data file
description: YugabyteDB Voyager import data file reference
menu:
  preview_yugabyte-voyager:
    identifier: voyager-import-data-file
    parent: bulk-data-load-ref
    weight: 70
type: docs
---

Load data from files in CSV or text format directly to the YugabyteDB database. These data files can be located either on a local filesystem, an AWS S3 bucket, GCS bucket, or an Azure blob. For more details, see [Bulk data load from files](../../../migrate/bulk-data-load/).

#### Syntax

```text
Usage: yb-voyager import data file [ <arguments> ... ]
```

#### Arguments

The valid *arguments* for import data file are described in the following table:

| Argument | Description/valid options |
| :------- | :------------------------ |
| --batch-size <number> | Size of batches generated for ingestion during import data. (default: 20000; rows) |
| --data-dir <path> | Path to the location of the data files to import; this can be a local directory or a URL for a cloud storage location such as an AWS S3 bucket, GCS bucket, or an Azure blob. For more details, see [Bulk data load from files](../../../migrate/bulk-data-load/).|
| --delimiter | Character used as delimiter in rows of the table(s). (default: comma (,) for CSV file format and tab (\t) for TEXT file format.) |
| --disable-pb | Use this argument to not display progress bars. For live migration, `--disable-pb` can also be used to hide metrics for import data. (default: false) |
| --escape-char | Escape character (default double quotes `"`) only applicable to CSV file format. |
| --file-opts <string> | **[Deprecated]** Comma-separated string options for CSV file format. <br>Options:<ul><li>`escape_char` - escape character</li><li>`quote_char` - character used to quote the values</li></ul>Default: double quotes (") for both escape and quote characters<br>Note that escape_char and quote_char are only valid and required for CSV file format.<br>Example: `--file-opts "escape_char=\",quote_char=\""` or `--file-opts 'escape_char=",quote_char="'` |
| --null-string | String that represents null value in the data file. (default: ""(empty string) for CSV, and '\N' for text.) |
| --file-table-map \<filename1:tablename1\> | Comma-separated mapping between the files in `--data-dir` argument to the corresponding table in the database. You can import multiple files in one table either by providing one `<fileName>:<tableName>` entry for each file OR by passing a glob expression in place of the file name. For example, `fileName1:tableName,fileName2:tableName` OR `fileName*:tableName`. |
| --format <format> | One of `CSV` or `text` format of the data file. (default: CSV) |
| --has-header | Applies only to CSV file type. (default: false; change to true if the CSV file contains column names as a header.)<br>**Note**: Boolean flags take arguments in the format `--flag-name=[true|false]`, and not `--flag-name [true|false]`. |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help. |
| --parallel-jobs <connectionCount> | Number of parallel COPY commands issued to the target database. Depending on the YugabyteDB database configuration, the value of `--parallel-jobs` should be tweaked such that at most 50% of target cores are utilised. (default: If yb-voyager can determine the total number of cores N in the YugabyteDB database cluster, it uses N/2 as the default. Otherwise, it defaults to twice the number of nodes in the cluster.)|
| --quote-char | Character used to quote the values (default double quotes `"`) only applicable to CSV file format. |
| --send-diagnostics | Send diagnostics information to Yugabyte. |
| --start-clean | Starts a fresh import with data files present in the `data` directory and if any table on YugabyteDB database is non-empty, it prompts whether you want to continue the import without truncating those tables; if yes, then yb-voyager starts ingesting the data present in the data files with upsert mode and for the cases where a table doesn't have a primary key, it may duplicate the data. In that case, use `--exclude-table-list` flag to exclude such tables or truncate those tables manually before using the `start-clean` flag. |
| --target-db-name <name> | Target database name. |
| --target-db-password <password>| Target database password. Alternatively, you can also specify the password by setting the environment variable `TARGET_DB_PASSWORD`. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes. |
| --target-db-port <port> | Port number of the target database machine. (default: 5433) |
| --target-db-schema <schemaName> | Schema name of the target database. MySQL and Oracle migrations only. |
| --target-db-user <username> | Username of the target database. |
| [--target-ssl-cert](../../yb-voyager-cli/#ssl-connectivity) <certificateName> | Name of the certificate which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-key](../../yb-voyager-cli/#ssl-connectivity) <keyName> | Name of the key which is part of the SSL `<cert,key>` pair. |
| [--target-ssl-crl](../../yb-voyager-cli/#ssl-connectivity) <path> | Path to a file containing the SSL certificate revocation list (CRL).|
| [--target-ssl-mode](../../yb-voyager-cli/#ssl-connectivity) <SSLmode> | One of `disable`, `allow`, `prefer`(default), `require`, `verify-ca`, or `verify-full`. |
| [--target-ssl-root-cert](../../yb-voyager-cli/#ssl-connectivity) <path> | Path to a file containing SSL certificate authority (CA) certificate(s). |
| --verbose | Display extra information in the output. (default: false) |
| -y, --yes  | Answer yes to all prompts during the export schema operation. |

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
