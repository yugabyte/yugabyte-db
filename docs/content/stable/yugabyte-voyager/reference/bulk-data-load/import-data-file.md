---
title: import data file reference
headcontent: yb-voyager import data file
linkTitle: import data file
description: YugabyteDB Voyager import data file reference
menu:
  stable_yugabyte-voyager:
    identifier: voyager-import-data-file
    parent: bulk-data-load-ref
    weight: 70
type: docs
---

Load data from files in CSV or text format directly to the YugabyteDB database. These data files can be located either on a local filesystem, an AWS S3 bucket, GCS bucket, or an Azure blob. For more details, see [Bulk data load from files](../../../migrate/bulk-data-load/).

## Syntax

```text
Usage: yb-voyager import data file [ <arguments> ... ]
```

### Arguments

The following table lists the valid CLI flags and parameters for `import data file` command.

When run at the same time, flags take precedence over configuration flag settings.

{{<table>}}

| <div style="width:150px">CLI flag</div> | Config file parameter | Description |
| :--- | :-------- | :---------- |

| --batch-size  |

```yaml{.nocopy}
import-data-file:
  batch-size:
```

| Size of batches in the number of rows generated for ingestion during import data. <br> Default: 20000 rows |

| --data-dir |

```yaml{.nocopy}
import-data-file:
  data-dir:
```

| Path to the location of the data files to import; this can be a local directory or a URL for a cloud storage location such as an AWS S3 bucket, GCS bucket, or an Azure blob. For more details, see [Bulk data load from files](../../../migrate/bulk-data-load/).|

| --delimiter |

```yaml{.nocopy}
import-data-file:
  delimiter:
```

| Character used as a delimiter to separate column values in rows of the datafile(s). <br> Default: comma `','` for CSV file format and tab `'\t'` for TEXT file format.<br>Example: `yb-voyager import data file .... --delimiter ','` |

| --disable-pb |

```yaml{.nocopy}
import-data-file:
  disable-pb:
```

| Use this argument to disable progress bar or statistics during data import. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1|

| --adaptive-parallelism |

```yaml{.nocopy}
import-data-file:
  adaptive-parallelism:
```

| Adjust parallelism based on the resource usage (CPU, memory) of the target YugabyteDB cluster. Choose from the following modes: <ul><li> `balanced` (Default): Run with moderate thresholds. Recommended when other workloads are running on the cluster.</li><li>`aggressive`: Run with maximum CPU thresholds for better performance. Recommended when no other workloads are running on the cluster.</li><li> `disabled`: Disable adaptive parallelism. </ul> |

| --adaptive-parallelism-max |

```yaml{.nocopy}
import-data-file:
  adaptive-parallelism-max:
```

| Number of maximum parallel jobs to use while importing data when adaptive parallelism is enabled. By default, voyager tries to determine the total number of cores `N` and use `N/2` as the maximum parallel jobs. |

| --enable-upsert |

```yaml{.nocopy}
import-data-file:
  enable-upsert:
```

| Enable UPSERT mode on target tables while importing data.<br> Note: Ensure that tables on the target YugabyteDB database do not have secondary indexes. If a table has secondary indexes, setting this flag to true may lead to corruption of the indexes. <br>Default: false<br> Usage for disabling the mode: `yb-voyager import data status ... --enable-upsert false` |

| --escape-char |

```yaml{.nocopy}
import-data-file:
  escape-char:
```

| Escape character <br> Default: double quotes `'"'`<br>Example: `yb-voyager import data file ... --escape-char '"'` |

| --null-string |

```yaml{.nocopy}
import-data-file:
  null-string:
```

| String that represents null values in the datafile. <br> Default: `""` (empty string) for CSV, and `'\N'` for text.<br>Example: `yb-voyager import data file ... --null-string 'NULL'` |

| --file-table-map |

```yaml{.nocopy}
import-data-file:
  file-table-map:
```

| Comma-separated mapping between the files in `--data-dir` argument to the corresponding table in the database. You can import multiple files in one table either by providing one `<fileName>:<tableName>` entry for each file OR by passing a glob expression in place of the file name.<br>Example: `--file-table-map 'fileName1:tableName,fileName2:tableName'` OR `--file-table-map 'fileName*:tableName'`. |

| --format |

```yaml{.nocopy}
import-data-file:
  format:
```

| Format of the data file. One of `csv` or `text`. <br> Default: csv<br>Example: `yb-voyager import data file ... --format text` |

| --has-header |

```yaml{.nocopy}
import-data-file:
  has-header:
```

| For `csv` datafiles, use this argument if the datafile has a header with column names for the table. <br> Default: false <br>Example: `yb-voyager import data file ... --format csv --has-header true` <br> Accepted parameters: true, false, yes, no, 0, 1 |

| --parallel-jobs |

```yaml{.nocopy}
import-data-file:
  parallel-jobs:
```

| Number of parallel COPY commands issued to the target database. Depending on the YugabyteDB database configuration, the value of `--parallel-jobs` should be tweaked such that at most 50% of target cores are utilised. <br> Default: If yb-voyager can determine the total number of cores N in the YugabyteDB database cluster, it uses N/2 as the default. Otherwise, it defaults to twice the number of nodes in the cluster.|

| --quote-char |

```yaml{.nocopy}
import-data-file:
  quote-char:
```

| Character used to quote the values. <br> Default: double quotes `'"'`<br>Example: `yb-voyager import data file ... --quote-char '"'` |

| --use-public-ip |

```yaml{.nocopy}
import-data-file:
  use-public-ip:
```

| Use the node public IP addresses to distribute `--parallel-jobs` uniformly on data import. <br>Default: false<br> **Note** that you may need to configure the YugabyteDB cluster with public IP addresses by setting [server-broadcast-addresses](../../../../reference/configuration/yb-tserver/#server-broadcast-addresses).<br>Example: `yb-voyager import data status ... --use-public-ip true`<br> Accepted parameters: true, false, yes, no, 0, 1 |

| --target-endpoints |

```yaml{.nocopy}
import-data-file:
  target-endpoints:
```

| Comma-separated list of node endpoints to use for parallel import of data.<br>Default: Use all the nodes in the cluster. For example: "host1:port1,host2:port2" or "host1,host2". Note: use-public-ip flag is ignored if this is used. |
| --on-primary-key-conflict |

```yaml{.nocopy}
import-data-file:
  on-primary-key-conflict:
```

| Action to take on primary key conflict during data import.<br>Accepted parameters: <ul><li> `ERROR` (Default): Import in this mode fails if any primary key conflict is encountered, indicating such conflicts are unexpected.</li><li>`IGNORE`: Skips rows that have an existing primary key allowing the import to continue for the remaining data.</li></ul> |

| -e, --export-dir |

```yaml{.nocopy}
  export-dir:
```

| Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|

| --target-db-name |

```yaml{.nocopy}
target:
  db-name:
```

| Target database name. |

| --target-db-password |

```yaml{.nocopy}
target:
  db-password:
```

| Target database password. Alternatively, you can also specify the password by setting the environment variable `TARGET_DB_PASSWORD`. If you don't provide a password via the CLI during any migration phase, yb-voyager will prompt you at runtime for a password. If the password contains special characters that are interpreted by the shell (for example, # and $), enclose the password in single quotes. |

| --target-db-port |

```yaml{.nocopy}
target:
  db-port:
```

| Port number of the target database machine. <br> Default: 5433 |

| --target-db-schema |

```yaml{.nocopy}
target:
  db-schema:
```

| Schema name of the target database. MySQL and Oracle migrations only. |

| --target-db-user |

```yaml{.nocopy}
target:
  db-user:
```

| Username of the target database. |

| --target-db-host |

```yaml{.nocopy}
target:
  db-host:
```

| Domain name or IP address of the machine on which the target database server is running. <br>Default: "127.0.0.1" |

| [--target-ssl-cert](../../yb-voyager-cli/#yugabytedb-options) |

```yaml{.nocopy}
target:
  ssl-cert:
```

| Path to a file containing the certificate which is part of the SSL `<cert,key>` pair. |

| [--target-ssl-key](../../yb-voyager-cli/#yugabytedb-options) |

```yaml{.nocopy}
target:
  ssl-key:
```

| Path to a file containing the key which is part of the SSL `<cert,key>` pair. |

| [--target-ssl-crl](../../yb-voyager-cli/#yugabytedb-options) |

```yaml{.nocopy}
target:
  ssl-crl:
```

| Path to a file containing the SSL certificate revocation list (CRL).|

| --target-ssl-mode |

```yaml{.nocopy}
target:
  ssl-mode:
```

| Specify the SSL mode for the target database as one of `disable`, `allow`, `prefer` (default), `require`, `verify-ca`, or `verify-full`. |

| [--target-ssl-root-cert](../../yb-voyager-cli/#yugabytedb-options) <path> |

```yaml{.nocopy}
target:
  ssl-root-cert:
```

| Path to a file containing SSL certificate authority (CA) certificate(s). |

| --file-opts | — | **[Deprecated]** Comma-separated string options for CSV file format. <br>Options:<ul><li>`escape_char` - escape character</li><li>`quote_char` - character used to quote the values</li></ul>Default: double quotes (") for both escape and quote characters<br>**Note** that escape_char and quote_char are only valid and required for CSV file format.<br>Example: `--file-opts "escape_char \\",quote_char \\""` or `--file-opts 'escape_char ",quote_char "'` |

|  --skip-replication-checks |

```yaml{.nocopy}
import-data-file:
  skip-replication-checks:
```

|  It is NOT recommended to have any form of replication (CDC/xCluster) running on the target YugabyteDB cluster during data import as it may lead to a rapid increase in disk use. If detected, data import is aborted. Use this flag to turn off the checks and continue importing data. <br>Default: false<br> Accepted parameters: true, false, yes, no, 0, 1 |

| --send-diagnostics |

```yaml{.nocopy}
send-diagnostics:
```

| Enable or disable sending [diagnostics](../../../reference/diagnostics-report/) information to Yugabyte. <br>Default: true<br> Accepted parameters: true, false, yes, no, 0, 1 |

| -l, --log-level |

```yaml {.nocopy}
log-level:
```

| Log level for yb-voyager. <br>Accepted values: trace, debug, info, warn, error, fatal, panic <br>Default: info |

| --truncate-tables |

```yaml{.nocopy}
import-data-file:
  truncate-tables:
```

| Truncate tables on target YugabyteDB database before importing data. This option is only valid if `--start-clean` is set to true. <br>Default: false |

| --error-policy |

```yaml{.nocopy}
import-data-file:
  error-policy:
```

| Specifies how to handle errors when processing and importing rows to the target YugabyteDB database. Errors can arise from reading data from file, transforming rows, or ingesting them into YugabyteDB. <br> Accepted parameters: <ul><li> `abort` (Default) - Immediately aborts the process.</li><li> `stash-and-continue` - Stashes the errored rows to a file and continues the import.</li></ul> |

| --start-clean | — | Starts a fresh import with data files present in the `data` directory.<br>If there's any non-empty table on the target YugabyteDB database, you get a prompt whether to continue the import without truncating those tables.<br> **Note** that for cases where a table doesn't have a primary key, it may lead to insertion of duplicate data. In that case, you can avoid the duplication by excluding the table from the `--file-table-map`, or truncating those tables manually before using the `start-clean` flag.<br> Default: false <br> Accepted parameters: true, false, yes, no, 0, 1 |
| -h, --help | — | Command line help. |
| -y, --yes | — | Answer yes to all prompts during the export schema operation. <br>Default: false |
| -c, --config-file | — | Path to a [configuration file](../../configuration-file). |

{{</table>}}

### Examples

#### Configuration file

```yaml
yb-voyager import data file --config-file <path-to-config-file>
```

You can specify additional parameters in the `import data file` section of the configuration file. For more details, refer to the [bulk-data-load.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/bulk-data-load.yaml) template.

#### CLI

The following examples use the `--data-dir` argument along with `--file-table-map` argument and contextually differ based on whether the data is imported from local disk or cloud storage options.

{{< tabpane text=true >}}

 {{% tab header="CSV" %}}

Import data from CSV files using `import data file` by providing the argument `--format csv` in the command.

### Import data file from local disk

The `--data-dir` argument is a path to the local directory where all the CSV files are present, and the `--file-table-map` argument provides a comma-separated mapping between each CSV file in `--data-dir` to the corresponding table in the database (you can mention case-sensitive table names as well, for example, `--file-table-map 'foo.csv:"Foo"'`); each file has a header, delimiter as `','` (default), and escape and quote character as `'"'` (default).

```sh
yb-voyager import data file --export-dir /dir/export-dir \
        --target-db-host 127.0.0.1 \
        --target-db-user ybvoyager \
        --target-db-password 'password' \
        --target-db-name target_db \
        --target-db-schema target_schema \
        --data-dir /dir/data-dir \
        --file-table-map 'accounts.csv:accounts,transactions.csv:transactions' \
        --format csv \
        --has-header true
```

### Import data file from AWS S3

The `--data-dir` argument is the AWS S3 URL of the data directory on the S3 bucket where all the CSV files are present, and the `--file-table-map` argument provides a comma-separated mapping between each CSV file in `--data-dir` to the corresponding table in the database, where each file has `'\t'` (tab character) as the delimiter, escape character as `'\'` and quote character as `"'"` with no header (default) as demonstrated in the following command:

```sh
yb-voyager import data file --export-dir /dir/export-dir \
        --target-db-host 127.0.0.1 \
        --target-db-user ybvoyager \
        --target-db-password 'password' \
        --target-db-name target_db \
        --target-db-schema target_schema \
        --data-dir s3://abc-mart/data \
        --file-table-map 'orders.csv:"Orders",products.csv:"Products",users.csv:"Users",order-items.csv:"Order_items",payments.csv:"Payments",reviews.csv:"Reviews",categories.csv:"Categories"' \
        --format csv \
        --delimiter '\t' \
        --escape-char '\' \
        --quote-char "'"
```

### Import data file from GCS

The `--data-dir` argument is the GCS URL of the data directory on the GCS bucket where all the CSV files are present, and the `--file-table-map` argument provides a comma-separated mapping of each CSV file in `--data-dir` to the corresponding table in the database, where each file has delimiter as `' '` (white space), escape character as `'^'`, quote character as `'"'`, and null string for null values as `'NULL'` with no header (default) as demonstrated in the following command:

```sh
yb-voyager import data file --export-dir /dir/export-dir \
        --target-db-host 127.0.0.1 \
        --target-db-user ybvoyager \
        --target-db-password 'password' \
        --target-db-name target_db \
        --target-db-schema target_schema \
        --data-dir gs://abc-bank/data \
        --file-table-map 'accounts.csv:accounts,transactions.csv:transactions' \
        --format csv \
        --delimiter ' ' \
        --null-string 'NULL' \
        --escape-char '^' \
        --quote-char '"'
```

### Import data file from Azure blob

The `--data-dir` argument is the Azure blob URL of the data directory on the Azure blob container where all the CSV files are present, and the `--file-table-map` argument provides a comma-separated mapping of each CSV file in `--data-dir` to the corresponding table in the database, where each file has delimiter `'#'`, escape character as `'%'`, quote character as `'"'`, and null string for null values as 'null' with no header (default) as demonstrated in the following command:

```sh
yb-voyager import data file --export-dir /dir/export-dir \
        --target-db-host 127.0.0.1 \
        --target-db-user ybvoyager \
        --target-db-password 'password' \
        --target-db-name target_db \
        --target-db-schema target_schema \
        --data-dir https://admin.blob.core.windows.net/air-world/data \
        --file-table-map 'airlines.csv:airlines,airports.csv:airports,flights.csv:flights,passengers.csv:passengers,bookings.csv:booking' \
        --format csv \
        --delimiter '#' \
        --null-string 'null' \
        --escape-char '%' \
        --quote-char '"'
```

### Load multiple files to the same table

Multiple files can be imported in one table (for example, `foo1.csv:foo,foo2.csv:foo` or `foo*.csv:foo`).

The `--data-dir` argument is a path to the local directory where all the CSV files are present, and the `--file-table-map` argument provides a comma-separated mapping between each CSV file in `--data-dir` to the corresponding table in the database, where each file has a header, delimiter as `','` (default), and escape and quote character as `'"'`(default).

Example for each file entry in --file-table-map (`foo1.csv:foo,foo2.csv:foo`) is as follows:

```sh
yb-voyager import data file --export-dir /dir/export-dir \
        --target-db-host 127.0.0.1 \
        --target-db-user ybvoyager \
        --target-db-password 'password' \
        --target-db-name target_db \
        --target-db-schema target_schema \
        --data-dir /dir/data-dir \
        --file-table-map 'accounts.csv:accounts,transactions1.csv:transactions,transactions2.csv:transactions' \
        --format csv \
        --has-header true
```

Example for glob expression of files in --file-table-map (`foo*.csv:foo`) is as follows:

```sh
yb-voyager import data file --export-dir /dir/export-dir \
        --target-db-host 127.0.0.1 \
        --target-db-user ybvoyager \
        --target-db-password 'password' \
        --target-db-name target_db \
        --target-db-schema target_schema \
        --data-dir /dir/data-dir \
        --file-table-map 'accounts.csv:accounts,transactions*.csv:transactions' \
        --format csv \
        --has-header true
```

{{% /tab %}}

{{% tab header="Text" %}}

Import data from text files using `import data file` by providing the argument `--format text` in the command.

### Import data file from local disk

The `--data-dir` argument is a path to the local directory where all the text files are present, and the `--file-table-map` argument provides a comma-separated mapping between each text file in `--data-dir` to the corresponding table in the database, where each file has `'\t'` (default) as a delimiter and `'\N'` (default) as a null string.

```sh
yb-voyager import data file --export-dir /dir/export-dir \
        --target-db-host 127.0.0.1 \
        --target-db-user ybvoyager \
        --target-db-password 'password' \
        --target-db-name target_db \
        --target-db-schema target_schema \
        --data-dir /dir/data-dir \
        --file-table-map 'accounts.txt:accounts,transactions.txt:transactions' \
        --format text
```

### Import data file from AWS S3

The `--data-dir` argument is the AWS S3 URL of the data directory on the S3 bucket where all the text files are present, and the `--file-table-map` argument provides a comma-separated mapping between each text file in `--data-dir` to the corresponding table in the database, where each file has `','` (comma) as the delimiter, and null string as `'NULL'` for null values as demonstrated in the following command:

```sh
yb-voyager import data file --export-dir /dir/export-dir \
        --target-db-host 127.0.0.1 \
        --target-db-user ybvoyager \
        --target-db-password 'password' \
        --target-db-name target_db \
        --target-db-schema target_schema \
        --data-dir s3://social-media/data \
        --file-table-map 'posts.txt:post,comments.txt:comments,profiles.txt:profiles,likes.txt:likes,messages.txt:messages,followers.txt:followers' \
        --format text \
        --delimiter ',' \
        --null-string 'NULL'
```

### Import data file from GCS

The `--data-dir` argument is the GCS URL of the data directory on the GCS bucket where all the text files are present, and the `--file-table-map` argument provides a comma-separated mapping of each text file in `--data-dir` to the corresponding table in the database (you can mention case-sensitive table names as well), where each file has delimiter as `'-'` (hyphen) as demonstrated in the following command:

```sh
yb-voyager import data file --export-dir /dir/export-dir \
        --target-db-host 127.0.0.1 \
        --target-db-user ybvoyager \
        --target-db-password 'password' \
        --target-db-name target_db \
        --target-db-schema target_schema \
        --data-dir gs://xyz-hospital/data \
        --file-table-map 'patients.txt:"Patients",doctors.txt:"Doctors",appointments.txt:"Appointments"' \
        --format text \
        --delimiter '-'
```

### Import data file from Azure blob

The `--data-dir` argument is the Azure blob URL of the data directory on the Azure blob container where all the text files are present, and the `--file-table-map` argument provides a comma-separated mapping of each text file in `--data-dir` to the corresponding table in the database, where each file has delimiter `'#'` (hash character), and null string as `'null'` for null values as demonstrated in the following command:

```sh
yb-voyager import data file --export-dir /dir/export-dir \
        --target-db-host 127.0.0.1 \
        --target-db-user ybvoyager \
        --target-db-password 'password' \
        --target-db-name target_db \
        --target-db-schema target_schema \
        --data-dir https://admin.blob.core.windows.net/weather-forecast/data \
        --file-table-map 'locations.txt:locations,weather.txt:weather,data.txt:data,forecasts.txt:forecasts' \
        --format text \
        --delimiter '#' \
        --null-string 'null'
```

### Load multiple files to the same table

Multiple files can be imported in one table (for example, `foo1.csv:foo,foo2.csv:foo` or `foo*.csv:foo`).

The `--data-dir` argument is a path to the local directory where all the text files are present, and the `--file-table-map` argument provides a comma-separated mapping between each text file in `--data-dir` to the corresponding table in the database  where each file has a header, delimiter as `'|'` (pipe character).


Example for each file entry in --file-table-map (`foo1.txt:foo,foo2.txt:foo`) is as follows:

```sh
yb-voyager import data file --export-dir /dir/export-dir \
        --target-db-host 127.0.0.1 \
        --target-db-user ybvoyager \
        --target-db-password 'password' \
        --target-db-name target_db \
        --target-db-schema target_schema \
        --data-dir /dir/academy-data \
        --file-table-map 'students1.txt:students,students2.txt:students,students3.txt:students,courses.txt:courses,instructors1.txt:instructors,instructors2.txt:instructors,enrollments.txt:enrollments,grades.txt:grades' \
        --format text \
        --delimiter '|'
```

Example for glob expression of files in --file-table-map (`foo*.txt:foo`) is as follows:

```sh
yb-voyager import data file --export-dir /dir/export-dir \
        --target-db-host 127.0.0.1 \
        --target-db-user ybvoyager \
        --target-db-password 'password' \
        --target-db-name target_db \
        --target-db-schema target_schema \
        --data-dir /dir/academy-data \
        --file-table-map 'students*.txt:students,courses.txt:courses,instructors*.txt:instructors,enrollments.txt:enrollments,grades.txt:grades' \
        --format text \
        --delimiter '|'
```

{{% /tab %}}

{{< /tabpane >}}
