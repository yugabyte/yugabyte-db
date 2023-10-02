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
| --escape-char | Escape character (default double quotes `"`) |
| --file-opts <string> | **[Deprecated]** Comma-separated string options for CSV file format. <br>Options:<ul><li>`escape_char` - escape character</li><li>`quote_char` - character used to quote the values</li></ul>Default: double quotes (") for both escape and quote characters<br>Note that escape_char and quote_char are only valid and required for CSV file format.<br>Example: `--file-opts "escape_char=\",quote_char=\""` or `--file-opts 'escape_char=",quote_char="'` |
| --null-string | String that represents null value in the data file. (default: ""(empty string) for CSV, and '\N' for text.) |
| --file-table-map \<filename1:tablename1\> | Comma-separated mapping between the files in `--data-dir` argument to the corresponding table in the database. You can import multiple files in one table either by providing one `<fileName>:<tableName>` entry for each file OR by passing a glob expression in place of the file name. For example, `fileName1:tableName,fileName2:tableName` OR `fileName*:tableName`. |
| --format <format> | One of `CSV` or `text` format of the data file. (default: CSV) |
| --has-header | default: false; change to true if the CSV file contains column names as a header.<br>**Note**: Boolean flags take arguments in the format `--flag-name=[true|false]`, and not `--flag-name [true|false]`. |
| -e, --export-dir <path> | Path to the export directory. This directory is a workspace used to store exported schema DDL files, export data files, migration state, and a log file.|
| -h, --help | Command line help. |
| --parallel-jobs <connectionCount> | Number of parallel COPY commands issued to the target database. Depending on the YugabyteDB database configuration, the value of `--parallel-jobs` should be tweaked such that at most 50% of target cores are utilised. (default: If yb-voyager can determine the total number of cores N in the YugabyteDB database cluster, it uses N/2 as the default. Otherwise, it defaults to twice the number of nodes in the cluster.)|
| --quote-char | Character used to quote the values (default double quotes `"`) |
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

### Examples

The following examples use the `--data-dir` argument along with `--file-table-map` argument and contextually differ based on whether the data is imported from local disk or cloud storage options.

{{< tabpane text=true >}}

 {{% tab header="CSV" %}}

Import data for text files using `import data file` command by providing the argument `--format 'csv'` in the command.

#### Import data file from local disk

The `--data-dir` argument is a path to the local directory where all the CSV files are present, and the `--file-table-map` argument provides a comma-separated mapping between each CSV file in `--data-dir` to the corresponding table in the database (you can mention case-sensitive table names as well); each file has a header, delimiter as ',' (default), escape and quote character as '"'(default).

```sh
yb-voyager import data file --export-dir '/dir/export-dir' \
        --target-db-host 'x.x.x.x' \
        --target-db-user 'ybvoyager' \
        --target-db-password 'password' \
        --target-db-name 'target_db' \
        --target-db-schema 'target_schema' \
        --data-dir '/dir/data-dir' \
        --file-table-map 'accounts.csv:accounts,transactions.csv:transactions' \
        --format 'csv' \
        --has-header=true
```

#### Import data file from AWS S3

The `--data-dir` argument is the AWS S3 URL of the data directory on the S3 bucket where all the CSV files are present, and the `--file-table-map` argument provides a comma-separated mapping between each CSV file in `--data-dir` to the corresponding table in the database, where each file has '\t' (tab character) as the delimiter, escape character as '\' and quote character as ''' with no header (default) as demonstrated in the following command:

```sh
yb-voyager import data file --export-dir '/dir/export-dir' \
        --target-db-host 'x.x.x.x' \
        --target-db-user 'ybvoyager' \
        --target-db-password 'password' \
        --target-db-name 'target_db' \
        --target-db-schema 'target_schema' \
        --data-dir 's3://abc-mart/data' \
        --file-table-map 'orders.csv:"Orders",products.csv:"Products",users.csv:"Users",order-items.csv:"Order_items",payments.csv:"Payments",reviews.csv:"Reviews",categories.csv:"Categories"' \
        --format 'csv' \
        --delimiter '\t' \
        --escape-char '\' \
        --quote-char "'"
```

#### Import data file from GCS

The `--data-dir` argument is the GCS URL of the data directory on the GCS bucket where all the CSV files are present, and the `--file-table-map` argument provides a comma-separated mapping of each CSV file in `--data-dir` to the corresponding table in the database, where each file has delimiter as ' ' (white space), escape character as '^', quote character as '"', and null string for null values as 'NULL' with no header (default) as demonstrated in the following command:

```sh
yb-voyager import data file --export-dir '/dir/export-dir' \
        --target-db-host 'x.x.x.x' \
        --target-db-user 'ybvoyager' \
        --target-db-password 'password' \
        --target-db-name 'target_db' \
        --target-db-schema 'target_schema' \
        --data-dir 'gs://abc-bank/data' \
        --file-table-map 'accounts.csv:accounts,transactions.csv:transactions' \
        --format 'csv' \
        --delimiter ' ' \
        --null-string 'NULL' \
        --escape-char '^' \
        --quote-char '"'
```

#### Import data file from Azure blob

The `--data-dir` argument is the Azure blob URL of the data directory on the Azure blob container where all the CSV files are present, and the `--file-table-map` argument provides a comma-separated mapping of each CSV file in `--data-dir` to the corresponding table in the database, where each file has delimiter '#', escape character as '%', quote character as '"', and null string for null values as 'null' with no header (default) as demonstrated in the following command:

```sh
yb-voyager import data file --export-dir '/dir/export-dir' \
        --target-db-host 'x.x.x.x' \
        --target-db-user 'ybvoyager' \
        --target-db-password 'password' \
        --target-db-name 'target_db' \
        --target-db-schema 'target_schema' \
        --data-dir 'https://admin.blob.core.windows.net/air-world/data' \
        --file-table-map 'airlines.csv:airlines,airports.csv:airports,flights.csv:flights,passengers.csv:passengers,bookings.csv:booking' \
        --format 'csv' \
        --delimiter '#' \
        --null-string 'null' \
        --escape-char '%' \
        --quote-char '"'
```

#### Incremental loading

Multiple files can be imported in one table (for example, `foo1.csv:foo,foo2.csv:foo` or `foo*.csv:foo`).

The `--data-dir` argument is a path to the local directory where all the CSV files are present, and the `--file-table-map` argument provides a comma-separated mapping between each CSV file in `--data-dir` to the corresponding table in the database, where each file has a header, delimiter as ',' (default), escape and quote character as '"'(default).

Example for each file entry in --file-table-map (`foo1.csv:foo,foo2.csv:foo`) is as follows:

```sh
yb-voyager import data file --export-dir '/dir/export-dir' \
        --target-db-host 'x.x.x.x' \
        --target-db-user 'ybvoyager' \
        --target-db-password 'password' \
        --target-db-name 'target_db' \
        --target-db-schema 'target_schema' \
        --data-dir '/dir/data-dir' \
        --file-table-map 'accounts.csv:accounts,transactions1.csv:transactions,transactions2.csv:transactions' \
        --format 'csv' \
        --has-header=true
```

Example for glob expression of files in --file-table-map (`foo*.csv:foo`) is as follows:

```sh
yb-voyager import data file --export-dir '/dir/export-dir' \
        --target-db-host 'x.x.x.x' \
        --target-db-user 'ybvoyager' \
        --target-db-password 'password' \
        --target-db-name 'target_db' \
        --target-db-schema 'target_schema' \
        --data-dir '/dir/data-dir' \
        --file-table-map 'accounts.csv:accounts,transactions*.csv:transactions' \
        --format 'csv' \
        --has-header=true
```

 {{% /tab %}}

  {{% tab header="Text" %}}

Import data for text files using `import data file` by providing the argument --format 'text' in the command.

#### Import data file from local disk

The `--data-dir` argument is a path to the local directory where all the text files are present, and the `--file-table-map` argument provides a comma-separated mapping between each text file in `--data-dir` to the corresponding table in the database, where each file has '\t' (default) as a delimiter and '\N' (default) as a null string.

```sh
yb-voyager import data file --export-dir '/dir/export-dir' \
        --target-db-host 'x.x.x.x' \
        --target-db-user 'ybvoyager' \
        --target-db-password 'password' \
        --target-db-name 'target_db' \
        --target-db-schema 'target_schema' \
        --data-dir '/dir/data-dir' \
        --file-table-map 'accounts.txt:accounts,transactions.txt:transactions' \
        --format 'text'
```

#### Import data file from AWS S3

The `--data-dir` argument is the AWS S3 URL of the data directory on the S3 bucket where all the text files are present, and the `--file-table-map` argument provides a comma-separated mapping between each text file in `--data-dir` to the corresponding table in the database, where each file has ',' (comma) as the delimiter, and null string as ` NULL` for null values as demonstrated in the following command:

```sh
yb-voyager import data file --export-dir '/dir/export-dir' \
        --target-db-host 'x.x.x.x' \
        --target-db-user 'ybvoyager' \
        --target-db-password 'password' \
        --target-db-name 'target_db' \
        --target-db-schema 'target_schema' \
        --data-dir 's3://social-media/data' \
        --file-table-map 'posts.txt:post,comments.txt:comments,profiles.txt:profiles,likes.txt:likes,messages.txt:messages,followers.txt:followers' \
        --format 'text' \
        --delimiter ',' \
        --null-string 'NULL'
```

#### Import data file from GCS

The `--data-dir` argument is the GCS URL of the data directory on the GCS bucket where all the text files are present, and the `--file-table-map` argument provides a comma-separated mapping of each text file in `--data-dir` to the corresponding table in the database (you can mention case-sensitive table names as well), where each file has delimiter as '-' (hyphen character) as demonstrated in the following command:

```sh
yb-voyager import data file --export-dir '/dir/export-dir' \
        --target-db-host 'x.x.x.x' \
        --target-db-user 'ybvoyager' \
        --target-db-password 'password' \
        --target-db-name 'target_db' \
        --target-db-schema 'target_schema' \
        --data-dir 'gs://xyz-hospital/data' \
        --file-table-map 'patients.txt:"Patients",doctors.txt:"Doctors",appointments.txt:"Appointments"'\
        --format 'text' \
        --delimiter '-'
```

#### Import data file from Azure blob

The `--data-dir` argument is the Azure blob URL of the data directory on the Azure blob container where all the text files are present, and the `--file-table-map` argument provides a comma-separated mapping of each text file in `--data-dir` to the corresponding table in the database , where each file has delimiter '#' (white space), and null string as 'null' for null values as demonstrated in the following command:

```sh
yb-voyager import data file --export-dir '/dir/export-dir' \
        --target-db-host 'x.x.x.x' \
        --target-db-user 'ybvoyager' \
        --target-db-password 'password' \
        --target-db-name 'target_db' \
        --target-db-schema 'target_schema' \
        --data-dir 'https://admin.blob.core.windows.net/weather-forecast/data' \
        --file-table-map 'locations.txt:locations,weather.txt:weather,data.txt:data,forecasts.txt:forecasts' \
        --format 'text' \
        --delimiter '#' \
        --null-string 'null'
```

#### Incremental loading

Multiple files can be imported in one table (for example, `foo1.csv:foo,foo2.csv:foo` or `foo*.csv:foo`).

The `--data-dir` argument is a path to the local directory where all the text files are present, and the `--file-table-map` argument provides a comma-separated mapping between each text file in `--data-dir` to the corresponding table in the database  where each file has a header, delimiter as '|' (pipe character).

Example for each file entry in --file-table-map (`foo1.csv:foo,foo2.csv:foo`) is as follows:

```sh
yb-voyager import data file --export-dir '/dir/export-dir' \
        --target-db-host 'x.x.x.x' \
        --target-db-user 'ybvoyager' \
        --target-db-password 'password' \
        --target-db-name 'target_db' \
        --target-db-schema 'target_schema' \
        --data-dir '/dir/academy-data' \
        --file-table-map 'students1.txt:students,students2.txt:students,students3.txt:students,courses.txt:courses,instructors1.txt:instructors,instructors2.txt:instructors,enrollments.txt:enrollments,grades.txt:grades' \
        --format 'text' \
        --delimiter '|'
```

Example for glob expression of files in --file-table-map (`foo*.csv:foo`) is as follows:

```sh
yb-voyager import data file --export-dir '/dir/export-dir' \
        --target-db-host 'x.x.x.x' \
        --target-db-user 'ybvoyager' \
        --target-db-password 'password' \
        --target-db-name 'target_db' \
        --target-db-schema 'target_schema' \
        --data-dir '/dir/academy-data' \
        --file-table-map 'students*.txt:students,courses.txt:courses,instructors*.txt:instructors,enrollments.txt:enrollments,grades.txt:grades' \
        --format 'text' \
        --delimiter '|'
```

  {{% /tab %}}