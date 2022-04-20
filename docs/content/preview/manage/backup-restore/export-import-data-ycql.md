---
title: Export and Import Data for YCQL
headerTitle: Export and Import Data
linkTitle: Export and Import Data
description: Export and Import Data for YCQL
menu:
  latest:
    identifier: export-import-data-ycql
    parent: backup-restore
    weight: 703
isTocNested: true
showAsideToc: true
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

Export and import of the YCQL data is performed by various commands of the `ycqlsh` script. For details on how to use the script to connect to a YugabyteDB cluster, as well as the full list of available parameters, refer to the [ycqlsh reference](../../../admin/ycqlsh/).

## Export Schema

To export the schema, use the `DESCRIBE` command (you can also use `DESC`, which is its shorter alias).

In particular, if you want to export the schema of a particular keyspace, run the following command:

```sh
./bin/ycqlsh -e "DESC KEYSPACE <keyspace-name>" > <file>
```

Where:
- `<keyspace-name>` – the name of the keyspace to be exported
- `<file>` – the path to the resulting SQL script file

Alternatively, to export the schema across all available keyspaces, run the following command:

```sh
ycqlsh -e "DESC SCHEMA" > <file>
```

Where:
- `<file>` – the path to the resulting CQL script file

## Export Data

To export the data, use the `COPY TO` command. It will query all rows of the specified table and write into a CSV (comma-separated values) file.

The full syntax of the `COPY TO` command is the following:

```sql
COPY table_name [( column_list )]
TO 'file_name'[, 'file2_name', ...] | STDIN
[WITH option = 'value' [AND ...]]
```

Where:
- `table_name` - name of the table to be exported
- `column-list` - optional comma-separated list of column names
- `file-name` - name of the file to export the data to

The command also supports multiple options. The following table outlines some of the more commonly used ones.

| Option  | Description | Default |
| :--------------- | :---------------- | :---------------- |
| DELIMITER | Character used to separate fields. | , (comma) |
| HEADER | Boolean value (`true` or `false`). If true, inserts the column names in the first row of data on exports. | false |
| PAGESIZE | Page size for fetching results. | 1000 |
| PAGETIMEOUT | Page timeout for fetching results. | 10 |
| MAXREQUESTS | Maximum number of requests each worker can process in parallel. | 6 |
| MAXOUTPUTSIZE | Maximum size of the output file, measured in number of lines. When set, the output file is split into segments when the value is exceeded. Use `-1` for no maximum. | -1 |

## Import Schema

To import the schema, use the SOURCE command as shown below:

```sh
ycqlsh -e "SOURCE '<file>'"
```

Where:
- `<file>` – the path to the CQL script file to import the schema from

## Import Data

After the schema is imported, you can import the actual data by using the `COPY FROM` command that loads the data from one or more CSV files.

Here is the full syntax of the `COPY FROM` command:

```sql
COPY table_name [( column_list )]
FROM 'file_name'[, 'file2_name', ...] | STDIN
[WITH option = 'value' [AND ...]]
```

Where:
- `table_name` - name of the table import the data to
- `column-list` - optional comma-separated list of column names
- `file-name` - name of the file to import the data from

Some of the commonly used options are:

| Option  | Description | Default |
| :--------------- | :---------------- | :---------------- |
| DELIMITER | Character used to separate fields. | `,` (comma) |
| HEADER    | Boolean value (`true` or `false`). If true, the first row of data contains column names. | false |
| CHUNKSIZE | The chunk size for each insert. | 1000 |
| INGESTRATE | Desired ingest rate in rows per second. Must be greater than CHUNKSIZE. | 100000 |
