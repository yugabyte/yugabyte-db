---
title: cqlsh
linkTitle: cqlsh
description: cqlsh
aliases:
  - /develop/tools/cqlsh/
menu:
  1.0:
    identifier: cqlsh
    parent: tools
    weight: 546
---

## Overview

`cqlsh` is a command-line shell for interacting with YugaByte DB through YCQL. It is installed as part of YugaByte DB and is located in the bin directory of YugaByte home. It is also available for download and install from [YugaByte's Github repository](https://github.com/YugaByte/cqlsh/releases).

## Example

```{.sh .copy .separator-dollar}
$ ./bin/cqlsh --execute "select cluster_name, data_center, rack from system.local" 127.0.0.1
```
```
 cluster_name  | data_center | rack
---------------+-------------+-------
 local cluster | datacenter1 | rack1
```

## Command-line Options

Use the **-\-help** option to see all the command-line options supported.

```
cqlsh [options] [host [port]]
```

Where

- `host` is the IP address of the host on which [YB-TServer](/architecture/concepts/universe/#yb-tserver) is run. The default is local host at 127.0.0.1.
- `port` is the TCP port at which YB-TServer listens for YCQL connections. The default is 9042.

Options | Short Form | Default | Description
----------------------------|----|-------|---------------------------------------
`--color` | `-C` |  | Force color output
`--no-color`| | | Disable color output
`--browser` | | | Specify the browser to use for displaying `cqlsh` help. This can be one of the [supported browser names](https://docs.python.org/2/library/webbrowser.html) (e.g. firefox) or a browser path followed by `%s` (e.g. `/usr/bin/google-chrome-stable %s`).
`--ssl` | | | Use SSL when connecting to YugaByte DB
`--user` | `-u` | | Username to authenticate against YugaByte DB with
`--password` | `-p` | | Password to authenticate against YugaByte DB with, should be used in conjunction with `--user`
`--keyspace` | `-k` | | Keyspace to authenticate to, should be used in conjunction with `--user`
`--file` | `-f` | | Execute commands from the given file, then exit
`--debug` | | | Print additional debugging information
`--encoding` | | UTF-8 | Specify a non-default encoding for output.
`--cqlshrc` | | | Specify the location for the `cqlshrc` file. The `cqlshrc` file holds configuration options for `cqlsh`. By default this is in the user’s home directory at `~/.cassandra/cqlsh`.
`--execute` | `-e` | | Execute the given statement, then exit
`--connect-timeout` | | 2 | Specify the connection timeout in seconds
`--request-timeout` | | 10 | Specify the request timeout in seconds
`--tty` | `-t` | | Force tty mode (command prompt)

## Special Commands

In addition to supporting regular YCQL statements, `cqlsh` also supports the following special commands.

### CONSISTENCY
```
CONSISTENCY <consistency level>
```

Sets the consistency level for the read operations that follow. Valid arguments include:

Consistency Level | Description
------------------|------------
`QUORUM` | Read the strongly consistent results from the tablet's quorum. The read request will be processed by the tablet leader only. This is the default consistency level.
`ONE` | Read from a follower with relaxed consistency guarantees.

To inspect the current consistency level, use `CONSISTENCY` with no arguments.

### SHOW VERSION
Prints the `cqlsh`, Cassandra, CQL, and native protocol versions in use. Example:

```{.sql .copy .separator-gt}
cqlsh> SHOW VERSION
```
```
[cqlsh 5.0.1 | Cassandra 3.8 | CQL spec 3.4.2 | Native protocol v4]
```

### SHOW HOST
Prints the IP address and port of the YB-TServer node that `cqlsh` is connected to in addition to the cluster name. Example:

```{.sql .copy .separator-gt}
cqlsh> SHOW HOST
```
```
Connected to local cluster at 127.0.0.1:9042.
```

### SOURCE
Reads the contents of a file and executes each line as a YCQL statement or special `cqlsh` command.

```
SOURCE '<file>'
```

Example usage:

```{.sql .copy .separator-gt}
cqlsh> SOURCE '/home/yugabyte/commands.cql'
```

### CAPTURE
Begins capturing command output and appending it to a specified file. Output will not be shown at the console while it is captured.

```
CAPTURE '<file>'
CAPTURE OFF
CAPTURE
```

- The path to the file to be appended to must be given inside a string literal. The path is interpreted relative to the current working directory. The tilde shorthand notation (`~/mydir`) is supported for referring to `$HOME`.
- Only query result output is captured. Errors and output from cqlsh-only commands will still be shown in the `cqlsh` session.
- To stop capturing output and show it in the `cqlsh` session again, use `CAPTURE OFF`.
- To inspect the current capture configuration, use `CAPTURE` with no arguments.

### HELP

Gives information about `cqlsh` commands. To see available topics, enter `HELP` without any arguments. To see help on a topic, use `HELP <topic>`. Also see the `--browser` argument for controlling what browser is used to display help.

```
HELP <topic>
```

### PAGING
Enables paging, disables paging, or sets the page size for read queries. When paging is enabled, only one page of data will be fetched at a time and a prompt will appear to fetch the next page. Generally, it’s a good idea to leave paging enabled in an interactive session to avoid fetching and printing large amounts of data at once.

```
PAGING ON
PAGING OFF
PAGING <page size in rows>
```
To inspect the current paging setting, use `PAGING` with no arguments.

### EXPAND
Enables or disables vertical printing of rows. Enabling EXPAND is useful when many columns are fetched, or the contents of a single column are large.

```
EXPAND ON
EXPAND OFF
```

To inspect the current expand setting, use `EXPAND` with no arguments.

### LOGIN
Authenticate as a specified YugaByte DB user for the current session.

```
LOGIN <username> [<password>]
```

### EXIT
Ends the current session and terminates the `cqlsh` process.

```
EXIT
QUIT
```

### CLEAR
Clears the console.

```
CLEAR
CLS
```

### DESCRIBE
Prints a description (typically a series of DDL statements) of a schema element or the cluster. This is useful for dumping all or portions of the schema.

```
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

The `DESCRIBE CLUSTER` command prints the cluster namer:

```{.sql .copy .separator-gt}
cqlsh> DESCRIBE CLUSTER
```
```
Cluster: local cluster
```

The `DESCRIBE SCHEMA` command prints the DDL statements needed to recreate the entire schema. This is especially useful for dumping the schema in order to clone a cluster or restore from a backup.

### COPY TO
Copies data from a table to a CSV file.

```
COPY <table name> [(<column>, ...)] TO <file name> WITH <copy option> [AND <copy option> ...]
```

If no columns are specified, all columns from the table will be copied to the CSV file. A subset of columns to copy may be specified by adding a comma-separated list of column names surrounded by parenthesis after the table name.

The `file name` should be a string literal (with single quotes) representing a path to the destination file. This can also the special value `STDOUT` (without single quotes) to print the CSV to stdout.

Options | Default | Description
--------|---------|------------
`MAXREQUESTS` | 6 | The maximum number token ranges to fetch simultaneously.
`PAGESIZE` | 1000 | The number of rows to fetch in a single page.
`PAGETIMEOUT` | 10 | The timeout in seconds per 1000 entries in the page size or smaller.
`BEGINTOKEN`, `ENDTOKEN` | | Token range to export. Defaults to exporting the full ring.
`MAXOUTPUTSIZE` | -1 | The maximum size of the output file measured in number of lines; beyond this maximum the output file will be split into segments. -1 means unlimited.
`ENCODING` | utf8 | The encoding used for characters.

The following options are common to both `COPY TO` and `COPY FROM`.

Options | Default | Description
--------|---------|------------
`NULLVAL` | `null` | The string placeholder for null values.
`HEADER` | `false` | For `COPY TO`, controls whether the first line in the CSV output file will contain the column names. For `COPY FROM`, specifies whether the first line in the CSV input file contains column names.
`DECIMALSEP` | `.` | The character that is used as the decimal point separator.
`THOUSANDSSEP` | | The character that is used to separate thousands. Defaults to the empty string.
`BOOLSTYlE` | `True,False` | The string literal format for boolean values.
`NUMPROCESSES` | | The number of child worker processes to create for `COPY` tasks. Defaults to a max of 4 for `COPY FROM` and 16 for `COPY TO`. However, at most (num_cores - 1) processes will be created.
`MAXATTEMPTS` | 5 | The maximum number of failed attempts to fetch a range of data (when using `COPY TO`) or insert a chunk of data (when using `COPY FROM`) before giving up.
`REPORTFREQUENCY` | 0.25 | How often status updates are refreshed, in seconds.
`RATEFILE` | | An optional file to output rate statistics to. By default, statistics are not output to a file.

### COPY FROM
Copies data from a CSV file to table.

```
COPY <table name> [(<column>, ...)] FROM <file name> WITH <copy option> [AND <copy option> ...]
```
If no columns are specified, all columns from the CSV file will be copied to the table. A subset of columns to copy may be specified by adding a comma-separated list of column names surrounded by parenthesis after the table name.

The `file name` should be a string literal (with single quotes) representing a path to the source file. This can also the special value `STDIN` (without single quotes) to read the CSV data from stdin.

Options | Default | Description
--------|---------|------------
`INGESTRATE` | 100000 | The maximum number of rows to process per second.
`MAXROWS` | -1 | The maximum number of rows to import. -1 means unlimited.
`SKIPROWS` | 0 | A number of initial rows to skip.
`SKIPCOLS` | | A comma-separated list of column names to ignore. By default, no columns are skipped.
`MAXPARSEERRORS` | -1 | The maximum global number of parsing errors to ignore. -1 means unlimited.
`MAXINSERTERRORS` | 1000 | The maximum global number of insert errors to ignore. -1 means unlimited.
`ERRFILE=` | | A file to store all rows that could not be imported, by default this is `import_<ks>_<table>.err` where `<ks>` is your keyspace and `<table>` is your table name.
`MAXBATCHSIZE` | 20 | The max number of rows inserted in a single batch.
`MINBATCHSIZE` | 2 | The min number of rows inserted in a single batch.
`CHUNKSIZE` | 1000 | The number of rows that are passed to child worker processes from the main process at a time.

See `COPY TO` for additional options common to both `COPY TO` and `COPY FROM`.
