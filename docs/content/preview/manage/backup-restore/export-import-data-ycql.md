---
title: Export and import for YCQL
headerTitle: Export and import
linkTitle: Export and import
description: Export and import for YCQL
aliases:
  - /preview/manage/backup-restore/back-up-data-ycql/
  - /preview/manage/backup-restore/restore-data-ycql/
menu:
  preview:
    identifier: export-import-data-ycql
    parent: backup-restore
    weight: 703
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../export-import-data/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="../export-import-data-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

You use the `ycqlsh` script to export and import YCQL data. For details on how to use the script to connect to a YugabyteDB cluster, as well as the full list of available options, see [ycqlsh reference](../../../admin/ycqlsh/).

## Export schema

You use the `DESCRIBE` command (abbreviated to `DESC`) to export the schema.

To export the schema of a single keyspace, run the following command:

```sh
./bin/ycqlsh -e "DESC KEYSPACE <keyspace_name>" > <file>
```

* *keyspace_name* is the name of the keyspace to be exported.
* *file* is the path to the resulting CQL script file.

To export the schema of every keyspace, run the following command:

```sh
ycqlsh -e "DESC SCHEMA" > <file>
```

 *file* is the path to the resulting CQL script file.

## Export data

To export your data, use the `COPY TO` command that queries all rows of the specified table and writes into a comma-separated values (CSV) file.

The following is a full syntax of the `COPY TO` command:

```output.sql
COPY table_name [( column_list )]
TO 'file_name'[, 'file2_name', ...] | STDIN
[WITH option = 'value' [AND ...]]
```

* *table_name* is the name of the table to be exported.
* *column_list* is an optional comma-separated list of column names.
* *file_name* is the name of the file to which to export the data.

The command supports multiple options. The following table outlines some of the more commonly used ones.

| Option | Description | Default value |
| :----- | :---------- | :------ |
| `DELIMITER` | Field separator character. | `,` (comma) |
| `HEADER` | If true, output column names as the first row of data. | `false` |
| `PAGESIZE` | Page size for fetching results. | 1000 |
| `PAGETIMEOUT` | Page timeout for fetching results. | 10 |
| `MAXREQUESTS` | Maximum number of requests each worker processes in parallel. | 6 |
| `MAXOUTPUTSIZE` | Maximum number of lines in the output file. <br/>When set, the output file is split into segments no larger than this value. <br/>Use `-1` for no maximum. | -1 |

## Import schema

To import a schema, use the `SOURCE` command, as follows:

```sh
ycqlsh -e "SOURCE '<file>'"
```

*file* is the path to the CQL script file from which to import the schema.

## Import data

After you import the schema, use the `COPY FROM` command to import the actual data from one or more CSV files.

The following is a full syntax of the `COPY FROM` command:

```output.sql
COPY table_name [( column_list )]
FROM 'file_name'[, 'file2_name', ...] | STDIN
[WITH option = 'value' [AND ...]]
```

* *table_name* is the name of the _destination_ table.
* *column_list* is an optional comma-separated list of column names.
* *file_name* is the name of the file from which to import the data.

The following table lists commonly-used options.

| Option | Description | Default |
| :----- | :---------- | :------ |
| `DELIMITER` | Field separator character. | `,` (comma) |
| `HEADER` | If true, the first row of data contains column names. | `false` |
| `CHUNKSIZE` | Maximum number of rows each insert. | 1000 |
| `INGESTRATE` | Desired ingestion rate in rows per second. <br/>Must be greater than `CHUNKSIZE`. | 100000 |

