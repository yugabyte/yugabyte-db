---
title: Back up data
headerTitle: Back up data
linkTitle: Back up data
description: Back up YCQL data in YugabyteDB.
menu:
  v2.12:
    identifier: back-up-data-ycql
    parent: backup-restore
    weight: 703
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../back-up-data" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="../back-up-data-ycql" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

To back up data, use [**ycqlsh**](../../../admin/ycqlsh/) with the [DESCRIBE](../../../admin/ycqlsh/#describe) and [COPY TO](../../../admin/ycqlsh/#copy-to) commands.

By default, ycqlsh connects to localhost at `127.0.0.1` and port `9042`. To connect to a different node, you can specify the host (and, optionally, port) after the command. For example:

```sh
$ ./bin/ycqlsh -e "DESC KEYSPACE myapp" > myapp_schema.cql 127.0.0.2
```

## Back up the schema using DESCRIBE

You perform schema backups using the `DESCRIBE` command, which can be shortened to `DESC`.

### Back up the schema for one keyspace

To back up the schema for a particular keyspace, run the following command.

```sh
$ ycqlsh -e "DESC KEYSPACE <keyspace name>" > schema.cql
```

### Back up the schema for an entire cluster

To back up the schema for all tables across all keyspaces, run the following command.

```sh
$ ycqlsh -e "DESC SCHEMA" > schema.cql
```

## Back up data using COPY TO

Use the `COPY TO` command to export the data from a table in CSV (comma separated value) format to a specified output file. `COPY TO` writes each row in the table to a separate line in the file, with column values separated by the delimiter.

`COPY TO` provides a number of options to help perform backups.

The syntax to specify options when using `COPY TO` is shown below.

```sql
COPY table_name [( column_list )]
TO 'file_name'[, 'file2_name', ...] | STDIN
[WITH option = 'value' [AND ...]]
```

The following table outlines some of the more commonly used options.

| Option  | Description | Default |
| :--------------- | :---------------- | :---------------- |
| DELIMITER | Character used to separate fields. | , (comma) |
| HEADER | Boolean value (`true` or `false`). If true, inserts the column names in the first row of data on exports. | false |
| PAGESIZE | Page size for fetching results. | 1000 |
| PAGETIMEOUT | Page timeout for fetching results. | 10 |
| MAXREQUESTS | Maximum number of requests each worker can process in parallel. | 6 |
| MAXOUTPUTSIZE | Maximum size of the output file, measured in number of lines. When set, the output file is split into segments when the value is exceeded. Use `-1` for no maximum. | -1 |

### Back up all columns of a table

All columns of the table are exported by default.

```sh
$ ycqlsh -e "COPY <keyspace>.<table> TO 'data.csv' WITH HEADER = TRUE;"
```

### Back up specific columns of a table

To back up selected columns of the table, specify the column names in a list.

```sh
$ ycqlsh -e "COPY <keyspace>.<table> (<column 1 name>, <column 2 name>, ...) TO 'data.csv' WITH HEADER = TRUE;"
```

## Example

This example assumes you have installed YugabyteDB and created a cluster. If not, follow the steps in [Quick start](../../../quick-start/).

This example uses the `myapp` keyspace and tables created in the Quick start. Refer to [Explore Yugabyte Cloud QL](../../../quick-start/explore/ycql/).

### Back up the schema

Run the following to back up the schema of the keyspace `myapp`.

```sh
$ ./bin/ycqlsh -e "DESC KEYSPACE myapp" > myapp_schema.cql
```

This saves the schema of the keyspace `myapp` along with its tables to the file `myapp_schema.cql`.

```sh
$ cat myapp_schema.cql
```

```sql
CREATE KEYSPACE myapp WITH replication = {'class': 'SimpleStrategy', 'replication_factor': '3'}  AND durable_writes = true;

CREATE TABLE myapp.stock_market (
    stock_symbol text,
    ts text,
    current_price float,
    PRIMARY KEY (stock_symbol, ts)
) WITH CLUSTERING ORDER BY (ts ASC)
    AND default_time_to_live = 0;
```

### Back up the table

Run the following command to back up the data in the table `myapp.stock_market`.

```sh
$ ./bin/ycqlsh -e "COPY myapp.stock_market TO 'myapp_data.csv' WITH HEADER = TRUE ;"
```

This saves all columns of the rows in the table `myapp.stock_market` to the file `myapp_data.csv`.

```sh
$ cat myapp_data.csv
```

```output
stock_symbol,ts,current_price
AAPL,2017-10-26 09:00:00,157.41
AAPL,2017-10-26 10:00:00,157
FB,2017-10-26 09:00:00,170.63
FB,2017-10-26 10:00:00,170.10001
GOOG,2017-10-26 09:00:00,972.56
GOOG,2017-10-26 10:00:00,971.90997
```

### Back up specific table columns

To back up a subset of columns, you can specify the columns in the backup command. The following example saves the `stock_symbol` and `ts` columns, but not the `current_price` column.

```sh
$ ./bin/ycqlsh -e "COPY myapp.stock_market (stock_symbol, ts) TO 'myapp_data_partial.csv' WITH HEADER = TRUE ;"
```

```sh
$ cat myapp_data_partial.csv
```

```output
stock_symbol,ts
AAPL,2017-10-26 09:00:00
AAPL,2017-10-26 10:00:00
FB,2017-10-26 09:00:00
FB,2017-10-26 10:00:00
GOOG,2017-10-26 09:00:00
GOOG,2017-10-26 10:00:00
```

## Next step

[Restore data in YCQL](../restore-data-ycql/)
