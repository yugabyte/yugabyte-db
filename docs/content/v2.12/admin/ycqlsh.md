---
title: ycqlsh - YCQL shell for YugabyteDB
headerTitle: ycqlsh
linkTitle: ycqlsh
description: Use the YCQL shell (ycqlsh) to interact with YugabyteDB.
menu:
  v2.12:
    identifier: ycqlsh
    parent: admin
    weight: 2460
type: docs
---

## Overview

The YCQL shell (`ycqlsh`) is a CLI for interacting with YugabyteDB using [YCQL](../../api/ycql/).

{{< note title="Note" >}}

The YCQL shell was previously named `cqlsh`. Although the `cqlsh` binary is available in the `bin` directory, it is deprecated and will be removed in a future release.

{{< /note >}}

`ycqlsh` is installed as part of YugabyteDB and is located in the `bin` directory of YugabyteDB home. You can also download it from the [cqlsh GitHub repository](https://github.com/yugabyte/cqlsh/releases).

If you prefer, you can install a standalone version using a shell script:

```sh
curl -sSL https://downloads.yugabyte.com/get_clients.sh | bash
```

If you have `wget`, you can use the following:

```sh
wget -q -O - https://downloads.yugabyte.com/get_clients.sh | sh
```

### Starting ycqlsh

```sh
./bin/ycqlsh
```

```output
Connected to local cluster at 127.0.0.1:9042.
[ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
ycqlsh>
```

### Online help

Run `ycqlsh --help` to display the online help.

## Syntax

```sh
ycqlsh [flags] [host [port]]
```

Where

- `host` is the IP address of the host on which [YB-TServer](../../architecture/concepts/universe/#yb-tserver-process) is run. The default is local host at `127.0.0.1`.
- `port` is the TCP port at which YB-TServer listens for YCQL connections. The default is `9042`.

### Example

```sh
./bin/ycqlsh --execute "select cluster_name, data_center, rack from system.local" 127.0.0.1
```

```output
 cluster_name  | data_center | rack
---------------+-------------+-------
 local cluster | datacenter1 | rack1
```

### Flags

| Flag | Short Form | Default | Description |
| :------| :--------- | :------ | :-----------|
| `--color` | `-C` | | Force color output. |
| `--no-color` | | | Disable color output. |
| `--browser` | | | Specify the browser to use for displaying `ycqlsh` help. This can be one of the [supported browser names](https://docs.python.org/2/library/webbrowser.html) (for example, Firefox) or a browser path followed by `%s` (for example, `/usr/bin/google-chrome-stable %s`). |
| `--ssl`  | | | Use SSL when connecting to YugabyteDB. |
| `--user`  | `-u` | | Username to authenticate against YugabyteDB with. |
| `--password` | `-p` |  | Password to authenticate against YugabyteDB with, should be used in conjunction with `--user`. |
| `--keyspace` | `-k` | | Keyspace to authenticate to, should be used in conjunction with `--user`. |
| `--file`  | `-f` |  | Execute commands from the given file, then exit. |
| `--debug` | | | Print additional debugging information. |
| `--encoding` | | UTF-8   | Specify a non-default encoding for output. |
| `--cqlshrc` | | | Specify the location for the `cqlshrc` file. The `cqlshrc` file holds configuration options for `ycqlsh`. By default this is in the user home directory at `~/.cassandra/cqlsh`. |
| `--execute` | `-e` | | Execute the given statement, then exit. |
| `--connect-timeout` | | 2 | Specify the connection timeout in seconds. |
| `--request-timeout` | | 10 | Specify the request timeout in seconds. |
| `--tty`  | `-t` | | Force tty mode (command prompt). |
| `--refresh_on_describe` | `-r` | | Force a refresh of the schema metadata on [DESCRIBE](#describe). |

### Save YCQL output to a file

To save output from a YCQL statement to a file, run the statement using the --execute flag, and redirect the output to a file.

For example, to save the output of a `SELECT` statement:

```sh
./bin/ycqlsh -e "SELECT * FROM mytable" > output.txt
```

## Special commands

In addition to supporting regular YCQL statements, `ycqlsh` also supports the following special commands.

### CONSISTENCY

```sql
CONSISTENCY <consistency level>
```

Sets the consistency level for the read operations that follow. Valid arguments include:

| Consistency Level | Description                                                  |
| ----------------- | ------------------------------------------------------------ |
| `QUORUM`          | Read the strongly consistent results from the tablet's quorum. The read request will be processed by the tablet leader only. This is the default consistency level. |
| `ONE`             | Read from a follower with relaxed consistency guarantees.    |

To view the current consistency level, use `CONSISTENCY` with no arguments.

### SHOW VERSION

Prints the `ycqlsh`, Cassandra, CQL, and native protocol versions in use. Example:

```sql
ycqlsh> SHOW VERSION
```

```output
[ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
```

### SHOW HOST

Prints the IP address and port of the YB-TServer server that `ycqlsh` is connected to, and the cluster name. Example:

```sql
ycqlsh> SHOW HOST
```

```output
Connected to local cluster at 127.0.0.1:9042.
```

### SOURCE

Reads the contents of a file and executes each line as a YCQL statement or special `ycqlsh` command.

```sh
SOURCE '<file>'
```

Example:

```sql
ycqlsh> SOURCE '/home/yugabyte/commands.cql'
```

### CAPTURE

Captures command output and appends it to the specified file. Output is not shown at the console while it is captured.

```sh
CAPTURE '<file>'
CAPTURE OFF
CAPTURE
```

- The path to the file to be appended to must be given inside a string literal. The path is interpreted relative to the current working directory. The tilde shorthand notation (`~/mydir`) is supported for referring to `$HOME`.
- Captures query result output only. Errors and output from ycqlsh-only commands are still shown in the `ycqlsh` session.
- To stop capturing output and show it in the `ycqlsh` session again, use `CAPTURE OFF`.
- To view the current capture configuration, use `CAPTURE` with no arguments.

### HELP

Gives information about `ycqlsh` commands. To see available topics, enter `HELP` without any arguments. To see help on a topic, use `HELP <topic>`. Also see the `--browser` argument for controlling what browser is used to display help.

```sql
HELP <topic>
```

### PAGING

Enables paging, disables paging, or sets the page size for read queries. When paging is enabled, only one page of data is fetched at a time and a prompt appears to fetch the next page. Generally, it's a good idea to leave paging enabled in an interactive session to avoid fetching and printing large amounts of data at once.

```sql
PAGING ON
PAGING OFF
PAGING <page size in rows>
```

To view the current paging setting, use `PAGING` with no arguments.

### EXPAND

Enables or disables vertical printing of rows. Use EXPAND when fetching many columns, or the contents of a single column are large.

```sql
EXPAND ON
EXPAND OFF
```

To view the current expand setting, use `EXPAND` with no arguments.

### LOGIN

Authenticate as a specified YugabyteDB user for the current session.

```sql
LOGIN <username> [<password>]
```

### EXIT

Ends the current session and terminates the `ycqlsh` process.

```sql
EXIT
QUIT
```

### CLEAR

Clears the console.

```sql
CLEAR
CLS
```

### DESCRIBE

Prints a description (typically a series of DDL statements) of a schema element or the cluster. Use DESCRIBE to dump all or portions of the schema.

```sql
DESCRIBE CLUSTER
DESCRIBE SCHEMA
DESCRIBE KEYSPACES
DESCRIBE KEYSPACE <keyspace name>
DESCRIBE TABLES
DESCRIBE TABLE <table name>
DESCRIBE INDEX <index name>
DESCRIBE TYPES
DESCRIBE TYPE <type name>
```

In any of the commands, `DESC` may be used in place of `DESCRIBE`.

The `DESCRIBE CLUSTER` command prints the cluster name:

```sql
ycqlsh> DESCRIBE CLUSTER
```

```output
Cluster: local cluster
```

The `DESCRIBE SCHEMA` command prints the DDL statements needed to recreate the entire schema. Use this command to dump the schema; you can then use the resulting file to clone the cluster or restore from a backup.

### COPY TO

Copies data from a table to a CSV file.

```sql
COPY <table name> [(<column>, ...)] TO <file name> WITH <copy flag> [AND <copy flag> ...]
```

By default, `COPY TO` copies all columns from the table to the CSV file. To copy a subset of columns, add a comma-separated list of column names enclosed in parentheses after the table name.

The `file name` should be a string literal (with single quotes) representing a path to the destination file. You can also use the special value `STDOUT` (without single quotes) to print the CSV to stdout.

| Flags                    | Default | Description                                                  |
| ------------------------ | ------- | ------------------------------------------------------------ |
| `MAXREQUESTS`            | 6       | The maximum number token ranges to fetch simultaneously.     |
| `PAGESIZE`               | 1000    | The number of rows to fetch in a single page.                |
| `PAGETIMEOUT`            | 10      | The timeout in seconds per 1000 entries in the page size or smaller. |
| `BEGINTOKEN`, `ENDTOKEN` |         | Token range to export. Defaults to exporting the full ring.  |
| `MAXOUTPUTSIZE`          | -1      | The maximum size of the output file measured in number of lines; beyond this maximum the output file will be split into segments. -1 means unlimited. |
| `ENCODING`               | utf8    | The encoding used for characters.                            |

The following flags are common to both `COPY TO` and `COPY FROM`.

| Flags             | Default      | Description                                                  |
| ----------------- | ------------ | ------------------------------------------------------------ |
| `NULLVAL`         | `null`       | The string placeholder for null values.                      |
| `HEADER`          | `false`      | For `COPY TO`, controls whether the first line in the CSV output file will contain the column names. For `COPY FROM`, specifies whether the first line in the CSV input file contains column names. |
| `DELIMITER`       | `,`          | The character that is used to separate fields (columns).     |
| `DECIMALSEP`      | `.`          | The character that is used as the decimal point separator.   |
| `THOUSANDSSEP`    |              | The character that is used to separate thousands. Defaults to the empty string. |
| `BOOLSTYlE`       | `True,False` | The string literal format for boolean values.                |
| `NUMPROCESSES`    |              | The number of child worker processes to create for `COPY` tasks. Defaults to a max of 4 for `COPY FROM` and 16 for `COPY TO`. However, at most (num_cores - 1) processes will be created. |
| `MAXATTEMPTS`     | 5            | The maximum number of failed attempts to fetch a range of data (when using `COPY TO`) or insert a chunk of data (when using `COPY FROM`) before giving up. |
| `REPORTFREQUENCY` | 0.25         | How often status updates are refreshed, in seconds.          |
| `RATEFILE`        |              | An optional file to output rate statistics to. By default, statistics are not output to a file. |

### COPY FROM

Copies data from a CSV file to table.

```sql
COPY <table name> [(<column>, ...)] FROM <file name> WITH <copy flag> [AND <copy flag> ...]
```

By default, `COPY FROM` copies all columns from the CSV file to the table. To copy a subset of columns, add a comma-separated list of column names enclosed in parentheses after the table name.

The `file name` should be a string literal (with single quotes) representing a path to the source file. Use the special value `STDIN` (without single quotes) to read the CSV data from stdin.

| Flags             | Default | Description                                                  |
| ----------------- | ------- | ------------------------------------------------------------ |
| `INGESTRATE`      | 100000  | The maximum number of rows to process per second.            |
| `MAXROWS`         | -1      | The maximum number of rows to import. -1 means unlimited.    |
| `SKIPROWS`        | 0       | A number of initial rows to skip.                            |
| `SKIPCOLS`        |         | A comma-separated list of column names to ignore. By default, no columns are skipped. |
| `MAXPARSEERRORS`  | -1      | The maximum global number of parsing errors to ignore. -1 means unlimited. |
| `MAXINSERTERRORS` | 1000    | The maximum global number of insert errors to ignore. -1 means unlimited. |
| `ERRFILE=`        |         | A file to store all rows that could not be imported; by default this is `import_<ks>_<table>.err` where `<ks>` is the keyspace and `<table>` is the table name. |
| `MAXBATCHSIZE`    | 20      | The max number of rows inserted in a single batch.           |
| `MINBATCHSIZE`    | 2       | The min number of rows inserted in a single batch.           |
| `CHUNKSIZE`       | 1000    | The number of rows that are passed to child worker processes from the main process at a time. |

See `COPY TO` for additional flags common to both `COPY TO` and `COPY FROM`.
