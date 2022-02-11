---
title: Restore data for YCQL
headerTitle: Restore data
linkTitle: Restore data
description: Restore data in YugabyteDB for YCQL
aliases:
  - /manage/backup-restore/backing-up-data
menu:
  latest:
    identifier: restore-data-ycql
    parent: backup-restore
    weight: 703
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/latest/manage/backup-restore/restore-data" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="/latest/manage/backup-restore/restore-data-ycql" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

## Restore a schema

To restore a schema, run the following command:

```sh
$ ycqlsh -e "source 'schema.cql'"
```

## Restore data from a backup

To restore data from a backup, run the following command.

```sh
$ ycqlsh -e "COPY <keyspace name>.<table name> FROM 'data.csv' WITH HEADER = TRUE ;"
```

You can restore data from a backup that has a subset of columns as well.

## Options

### Connect to a remote host and port

The default host is `127.0.0.1` and the default port is `9042`. You can override these values as shown below.

```sh
$ ycqlsh -e <command> <host> [<port>]
```

### Copy options

The `COPY FROM` command provides a number of options to help perform a restore.

The syntax to specify options when using `COPY FROM` is shown below.

```sql
COPY table_name [( column_list )]
FROM 'file_name'[, 'file2_name', ...] | STDIN
[WITH option = 'value' [AND ...]]
```

The following table outlines some of the more commonly used options.

| Option  | Description | Default |
| :--------------- | :---------------- | :---------------- |
| DELIMITER | Character used to separate fields. | `,` (comma) |
| HEADER    | Boolean value (`true` or `false`). If true, the first row of data contains column names. | false |
| CHUNKSIZE | The chunk size for each insert. | 1000 |
| INGESTRATE | Desired ingest rate in rows per second. Must be greater than CHUNKSIZE. | 100000 |

## Example

This example restores the backup performed on the [Back up data](../back-up-data-ycql/#example) page, and assumes you have the following files:

- `myapp_schema.cql` schema backup
- `myapp_data.csv` data backup

First, connect to the cluster using `ycqlsh` and drop the table with the data and the keyspace as follows:

```sql
ycqlsh> DROP TABLE myapp.stock_market;
```

You can drop the keyspace by running the following:

```sql
ycqlsh> DROP KEYSPACE myapp;
```

### Restore the table schema

The schema backup file `myapp_schema.cql` should appear as follows:

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

To import the schema, run the following:

```sh
$ ./bin/ycqlsh -f myapp_schema.cql
```

You can verify that the table was created by connecting to the cluster using `ycqlsh` and running the following:

```sql
ycqlsh> DESC myapp.stock_market;
```

```sql
CREATE TABLE myapp.stock_market (
    stock_symbol text,
    ts text,
    current_price float,
    PRIMARY KEY (stock_symbol, ts)
) WITH CLUSTERING ORDER BY (ts ASC)
    AND default_time_to_live = 0;
```

### Restore the data

The data backup file `myapp_data.csv` should appear as follows:

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

To restore the data, do the following:

```sh
$ ./bin/ycqlsh -e "COPY myapp.stock_market FROM 'myapp_data.csv' WITH HEADER = TRUE ;"
```

The procedure to import data from a partial backup is identical.

To verify that the data has been restored, connect to the cluster using `ycqlsh` and run the following query:

```sql
ycqlsh> SELECT * FROM myapp.stock_market;
```

```output
 stock_symbol | ts                  | current_price
--------------+---------------------+---------------
         GOOG | 2017-10-26 09:00:00 |        972.56
         GOOG | 2017-10-26 10:00:00 |     971.90997
         AAPL | 2017-10-26 09:00:00 |        157.41
         AAPL | 2017-10-26 10:00:00 |           157
           FB | 2017-10-26 09:00:00 |        170.63
           FB | 2017-10-26 10:00:00 |     170.10001

(6 rows)
```

## See also

[Back up data in YCQL](../back-up-data-ycql/)
