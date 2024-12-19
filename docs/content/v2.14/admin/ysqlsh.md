---
title: ysqlsh - YSQL shell for YugabyteDB
headerTitle: ysqlsh
linkTitle: ysqlsh
description: Use the YSQL shell (ysqlsh) to interact with YugabyteDB.
menu:
  v2.14:
    identifier: ysqlsh
    parent: admin
    weight: 2459
type: docs
---

## Overview

The YugabyteDB SQL shell (`ysqlsh`) provides a CLI for interacting with YugabyteDB using [YSQL](../../api/ysql/). It enables you to:

- interactively enter SQL queries and see the query results
- input from a file or the command line
- use meta-commands for scripting and administration

`ysqlsh` is installed with YugabyteDB and is located in the `bin` directory of the YugabyteDB home directory.

If you prefer, you can install a standalone version using any of the following methods:

- Using Docker:

    ```sh
    docker run -it yugabytedb/yugabyte-client ysqlsh -h <hostname> -p <port>
    ```

- Using Homebrew:

    ```sh
    brew tap yugabyte/tap
    brew install yugabytedb-client
    ysqlsh
    ```

- Using a shell script:

    ```sh
    curl -sSL https://software.yugabyte.com/get_clients.sh | bash
    ```

    If you have `wget`, you can use the following:

    ```sh
    wget -q -O - https://software.yugabyte.com/get_clients.sh | sh
    ```

`ysqlsh` works best with servers of the same or an older major version. [Meta-commands](#meta-commands) are particularly likely to fail if the server is a newer version than `ysqlsh` itself. The general functionality of running SQL statements and displaying query results should also work with servers of a newer major version, but this cannot be guaranteed in all cases.

If you are running multiple versions of YugabyteDB, use the newest version of `ysqlsh` to connect. You can keep and use the matching version of `ysqlsh` to use with each version of YugabyteDB, but in practice, this shouldn't be necessary.

### Starting ysqlsh

```sh
$ ./bin/ysqlsh
```

```output
ysqlsh (11.2-YB-2.0.0.0-b0)
Type "help" for help.

yugabyte=#
```

### Online help

Run `ysqlsh --help` to display the online help.

### Exit status

`ysqlsh` returns the following to the shell on exit:

- `0` if it finished normally
- `1` if a fatal error of its own occurs (for example, `out of memory`, `file not found`)
- `2` if the connection to the server went bad and the session wasn't interactive
- `3` if an error occurred in a script and the variable [ON_ERROR_STOP](#on-error-stop) was set

## Syntax

```sh
ysqlsh [ <option>...] [ <dbname> [ <username> ]]
```

### Default flags

When you open `ysqlsh`, the following default flags (aka flags) are set so that the user doesn't have to specify them.

- host: `-h 127.0.0.1`
- port: `-p 5433`
- user: `-U yugabyte`

{{< note title="Note" >}}

Starting with v2.0.1, the default password for the default user `yugabyte` is `yugabyte`. If YSQL authentication is enabled, then the `yugabyte` user is prompted for this password.

For v2.0.0 users, the default user `yugabyte` has no password. If you don't want any password to be prompted, don't enable YSQL authentication. If you want to enable YSQL authentication, then you must first set a password for the `yugabyte` user (in a cluster with YSQL authentication turned off).

{{< /note >}}

## Flags

##### -a, --echo-all

Print all nonempty input lines to standard output as they are read. (This doesn't apply to lines read interactively.) This is equivalent to setting the variable `ECHO` to `all`.

##### -A, --no-align

Switches to unaligned output mode. (The default output mode is otherwise aligned.) This is equivalent to `\pset format unaligned`.

##### -b, --echo-errors

Print failed SQL statements to standard error output. This is equivalent to setting the variable `ECHO` to `errors`.

##### -c *command*, --command=*command*

Specifies that `ysqlsh` is to execute the given command string, *command*. This flag can be repeated and combined in any order with the `-f` flag. When either `-c` or `-f` is specified, `ysqlsh` doesn't read commands from standard input; instead it terminates after processing all the `-c` and `-f` flags in sequence.

The command (*command*) must be either a command string that is completely parsable by the server (that is, it contains no `ysqlsh`-specific features), or a single backslash (`\`) command. Thus, you cannot mix SQL and `ysqlsh` meta-commands in a `-c` flag. To achieve that, you could use repeated `-c` flags or pipe the string into `ysqlsh`, for example:

```plpgsql
ysqlsh -c '\x' -c 'SELECT * FROM foo;'
```

or

```plpgsql
echo '\x \\ SELECT * FROM foo;' | ./bin/ysqlsh
```

(`\\` is the separator meta-command.)

Each SQL statement string passed to `-c` is sent to the server as a single query. Because of this, the server executes it as a single transaction even if the string contains multiple SQL statements, unless there are explicit `BEGIN` and `COMMIT` statements included in the string to divide it into multiple transactions. Also, `ysqlsh` only prints the result of the last SQL statement in the string. This is different from the behavior when the same string is read from a file or fed to `ysqlsh`'s standard input, because then `ysqlsh` sends each SQL statement separately.

Because of this behavior, putting more than one SQL statement in a single `-c` string often has unexpected results. It's better to use repeated `-c` commands or feed multiple commands to `ysqlsh`'s standard input, either using `echo` as illustrated above, or using a shell here-document, for example:

```plpgsql
./bin/ysqlsh<<EOF
\x
SELECT * FROM foo;
EOF
```

##### -d *dbname*, --dbname=*dbname*

Specifies the name of the database to connect to. This is equivalent to specifying *dbname* as the first non-option argument on the command line.

If this parameter contains an `=` sign or starts with a valid URI prefix (`yugabytedb://`), it is treated as a *conninfo* string.

##### -e, --echo-queries

Copy all SQL statements sent to the server to standard output as well. This is equivalent to setting the variable `ECHO` to `queries`.

##### -E, --echo-hidden

Echo the actual queries generated by [\d](#d-s-pattern-patterns) and other backslash commands. You can use this to study the internal operations of `ysqlsh`. This is equivalent to setting the variable [ECHO_HIDDEN](#echo-hidden) to `on`.

##### -f *filename*, --file=*filename*

Read statements from the file *filename*, rather than standard input. This option can be repeated and combined in any order with the `-c` option. When either `-c` or `-f` is specified, `ysqlsh` doesn't read statements from standard input; instead it terminates after processing all the `-c` and `-f` options in sequence. Except for that, this option is largely equivalent to the meta-command [\i](#i-filename-include-filename).

If *filename* is `-` (hyphen), then standard input is read until an EOF indication or [\q](#q-quit) meta-command. This can be used to intersperse interactive input with input from files. Note however that [Readline](#command-line-editing) isn't used in this case (much as if `-n` had been specified).

Using this option is subtly different from writing `ysqlsh < <filename>`. In general, both do what you expect, but using `-f` enables some nice features such as error messages with line numbers. There is also a slight chance that using this option reduces the start-up overhead. On the other hand, the variant using the shell's input redirection is (in theory) guaranteed to yield exactly the same output you would have received had you entered everything by hand.

##### -F *separator*, --field-separator=*separator*

Use *separator* as the field separator for unaligned output. This is equivalent to `\pset fieldsep` or `\f`.

##### -h *hostname*, --host=*hostname*

Specifies the host name of the machine on which the server is running. If the value begins with a slash (`/`), it is used as the directory for the Unix-domain socket. Default value is `127.0.0.1`.

##### -H, --html

Turn on HTML tabular output. This is equivalent to [\pset format html](#format) or the [\H](#h-html-1) command.

##### -l, --list

List all available databases, then exit. Other non-connection options are ignored. This is similar to the meta-command [`\list`](#l-list-pattern).

When this option is used, `ysqlsh` connects to the database `yugabyte`, unless a different database is named on the command line (flag `-d` or non-option argument, possibly using a service entry, but not using an environment variable).

##### -L *filename*, --log-file=*filename*

Write all query output into file *filename*, in addition to the normal output destination.

##### -n, --no-readline

Don't use [Readline](#command-line-editing) for line editing and don't use the command history. This can be used to turn off tab expansion when cutting and pasting.

##### -o *filename*, --output=*filename*

Put all query output into file *filename*. This is equivalent to the command `\o`.

##### -p *port*, --port=*port*

Specifies the TCP port or the local Unix-domain socket file extension on which the server is listening for connections. Defaults to the compiled-in value of `5433` unless the [PGPORT](#pgdatabase-pghost-pgport-pguser) environment variable is set.

##### -P *assignment* |--pset=*assignment*

Specifies printing options, in the style of [\pset](#pset-option-value). Note that here you have to separate name and value with an equal sign instead of a space. For example, to set the output format to LaTeX, you could write `-P format=latex`.

##### -q, --quiet

Specifies that `ysqlsh` should do its work quietly. By default, it prints welcome messages and various informational output. If this option is used, none of this happens. This is useful with the `-c` option. This is equivalent to setting the variable [QUIET](#quiet) to `on`.

##### -R *separator*, --record-separator=*separator*

Use *separator* as the record separator for unaligned output. This is equivalent to [\pset recordsep](#recordsep).

##### -s, --single-step

Run in single-step mode. That means the user is prompted before each command is sent to the server, with the option to cancel execution as well. Use this to debug scripts.

##### -S, --single-line

Runs in single-line mode where a newline terminates an SQL statement, as a semicolon does.

{{< note title="Note" >}}

This mode is provided for those who insist on it, but you aren't necessarily encouraged to use it. In particular, if you mix SQL statements and meta-commands on a line, the order of execution may not be clear to inexperienced users.

{{< /note >}}

##### -t, --tuples-only

Turn off printing of column names and result row count footers, etc. This is equivalent to `\t` or [\pset tuples_only](#tuples-only-or-t).

##### -T *table_options*, --table-attr=*table_options*

Specifies options to be placed in the HTML `table` tag. For details, see [\pset tableattr](#tableattr-or-t).

##### -U *username*, --username=*username*

Connect to the database as the user *username* instead of the default. (You must have permission to do so, of course.)

##### -v *assignment*, --set=*assignment*, --variable=*assignment*

Perform a variable assignment, like the [\set](#set-name-value) meta-command. Note that you must separate name and value, if any, by an equal sign (`=`) on the command line. To unset a variable, leave off the equal sign. To set a variable with an empty value, use the equal sign but leave off the value. These assignments are done during command line processing, so variables that reflect connection state are overwritten later.

##### -V, --version

Print the `ysqlsh` version and exit.

##### -w, --no-password

Never issue a password prompt. If the server requires password authentication and a password isn't available by other means such as a `~/.pgpass` file, the connection attempt fails. This option can be used in batch jobs and scripts where no user is present to enter a password.

Note that this option remains set for the entire session, and so it affects uses of the meta-command [\connect](#c-connect-reuse-previous-on-off-dbname-username-host-port-conninfo) as well as the initial connection attempt.

##### -W, --password

Force `ysqlsh` to prompt for a password before connecting to a database.

This option is never essential, as `ysqlsh` automatically prompts for a password if the server demands password authentication. However, `ysqlsh` wastes a connection attempt finding out that the server wants a password. In some cases it is worth typing `-W` to avoid the extra connection attempt.

Note that this option remains set for the entire session, and so it affects uses of the meta-command [\connect](#c-connect-reuse-previous-on-off-dbname-username-host-port-conninfo) as well as the initial connection attempt.

##### -x, --expanded

Turn on the expanded table formatting mode. This is equivalent to `\x` or [\pset expanded](#expanded-or-x).

##### -X, --no-psqlrc

Don't read the start-up file (neither the system-wide `psqlrc` file nor the user's `~/.psqlrc` file).

##### -z, --field-separator-zero

Set the field separator for unaligned output to a zero byte. This is equivalent to [\pset fieldsep_zero](#fieldsep-zero).

##### -0, --record-separator-zero

Set the record separator for unaligned output to a zero byte. This is useful for interfacing, for example, with `xargs -0`. This is equivalent to [\pset recordsep_zero](#recordsep-zero).

##### -1, --single-transaction

This option can only be used in combination with one or more `-c` and/or `-f` options. It causes `ysqlsh` to issue a [BEGIN](../../api/ysql/the-sql-language/statements/txn_begin/) statement before the first such option and a [COMMIT](../../api/ysql/the-sql-language/statements/txn_commit) statement after the last one, thereby wrapping all the commands into a single transaction. This ensures that either all the commands complete successfully, or no changes are applied.

If the statements themselves contain `BEGIN`, `COMMIT`, or [ROLLBACK](../../api/ysql/the-sql-language/statements/txn_rollback), this option won't have the desired effects. Also, if an individual statement cannot be executed inside a transaction block, specifying this option causes the whole transaction to fail.

##### -?, --help[=*topic*]

Show help about `ysqlsh` and exit. The optional *topic* parameter (defaulting to `options`) selects which part of `ysqlsh` is explained: `commands` describes `ysqlsh`'s backslash commands; `options` describes the command-line options that can be passed to `ysqlsh`; and `variables` shows help about `ysqlsh` configuration variables.

## Using ysqlsh

### Connect to a database

{{< note title="YugabyteDB Managed" >}}

For information on connecting to a YugabyteDB Managed cluster using ysqlsh, refer to [Connect via client shells](../../yugabyte-cloud/cloud-connect/connect-client-shell/).

{{< /note >}}

To connect to a database, you need the following information:

- the name of your target database
- the host name and port number of the server
- the user name you want to connect as

You provide these parameters using the `-d`, `-h`, `-p`, and `-U` flags.

`ysqlsh` provides the following defaults for these values:

- If you omit the host name, `ysqlsh` connects to the compiled-in default of `127.0.0.1` or a Unix-domain socket to a server on the local host, or using TCP/IP to localhost on machines that don't have Unix-domain sockets.
- The default port number is compiled-in as `5433`. Because the database server uses the same default, you don't have to specify the port in most cases.
- The default username is compiled-in as `yugabyte`, as is the default database name. If an argument is found that doesn't belong to any option, it is interpreted as the database name (or the user name, if the database name is already given).

{{< note title="Note" >}}

You can't just connect to any database under any user name. Your database administrator should have informed you of your access rights.

{{< /note >}}

When the defaults aren't quite right, you can save yourself some typing by setting the [environment variables](#environment-variables) `PGDATABASE`, `PGHOST`, `PGPORT`, and `PGUSER` to appropriate values. It is also convenient to have a `~/.pgpass` file to avoid regularly having to type in passwords.

An alternative way to specify connection parameters is in a *conninfo* string or a URI, which is used instead of a database name. This mechanism gives you wide control over the connection. For example:

```plpgsql
$ ysqlsh "service=myservice sslmode=require"
$ ysqlsh postgresql://dbmaster:5433/mydb?sslmode=require
```

If the connection can't be made for any reason (for example, insufficient privileges, server isn't running on the targeted host, etc.), `ysqlsh` [returns an error](#exit-status) and terminates.

If both standard input and standard output are a terminal, then `ysql` sets the client encoding to `auto`, which detects the appropriate client encoding from the locale settings (`LC_CTYPE` environment variable on Linux systems). If this doesn't work out as expected, the client encoding can be overridden using the environment variable `PGCLIENTENCODING`.

### Entering SQL statements

In normal operation, `ysqlsh` provides a [prompt](#prompting) with the name of the database to which `ysqlsh` is currently connected, followed by the string `=>`. For example:

```plpgsql
$ ysqlsh testdb
ysqlsh ()
Type "help" for help.

testdb=>
```

At the prompt, the user can type in SQL statements. Ordinarily, input lines are sent to the server when a command-terminating semicolon (`;`) is reached. An end of line doesn't terminate a statement. Thus, commands can be spread over several lines for clarity. If the statement was sent and executed without error, the results of the statement are displayed on the screen.

{{< note title="Note" >}}

If untrusted users have access to a database that hasn't adopted a secure schema usage pattern, begin your session by removing publicly-writable schemas from `search_path`. You can add `options=-csearch_path=` to the connection string or issue `SELECT pg_catalog.set_config('search_path', '', false)` before other SQL statements. This consideration isn't specific to `ysqlsh`; it applies to every interface for executing arbitrary SQL statements.

{{< /note >}}

{{< note title="Note" >}}

When using [\i](#i-filename-include-filename) to restore a YugabyteDB database, SQL script files generated by `ysql_dump` and `ysql_dumpall` issues the `SELECT pg_catalog.set_config('search_path', '', false)` statement before executing any other SQL statements.

{{< /note >}}

Whenever a statement is executed, `ysqlsh` also polls for asynchronous notification events generated by `LISTEN` and `NOTIFY`.

While C-style block comments are passed to the server for processing and removal, SQL-standard comments are removed by `ysqlsh`.

## Meta-commands

Anything you enter in `ysqlsh` that begins with an unquoted backslash is a meta-command that is processed by `ysqlsh` itself. These commands make `ysqlsh` more suitable for administration or scripting. Meta-commands are often called slash or backslash commands.

{{< note title="Cloud shell" >}}

For security reasons, the YugabyteDB Managed cloud shell only has access to a subset of meta-commands. With the exception of read-only access to the `/share` directory to load the sample datasets, commands that access the filesystem don't work in cloud shell.

{{< /note >}}

The format of a `ysqlsh` command is the backslash (`\`), followed immediately by a command verb, then any arguments. The arguments are separated from the command verb and each other by any number of whitespace characters.

To include whitespace in an argument you can quote it with single quotes (`' '`). To include a single quote in an argument, write two single quotes in single-quoted text (`' ... '' ...'`). Anything contained in single quotes is furthermore subject to C-like substitutions for `\n` (new line), `\t` (tab), `\b` (backspace), `\r` (carriage return), `\f` (form feed), `\digits` (octal), and `\xdigits` (hexadecimal). A backslash preceding any other character in single-quoted text quotes that single character, whatever it is.

If an unquoted colon (`:`) followed by a `ysqlsh` variable name appears in an argument, it is replaced by the variable's value, as described in [SQL Interpolation](#sql-interpolation). The forms `:'variable_name'` and `:"variable_name"` described there work as well.

In an argument, text that is enclosed in backquotes is taken as a command line that is passed to the shell. The output of the command (with any trailing newline removed) replaces the backquoted text. In the text enclosed in backquotes, no special quoting or other processing occurs, except that appearances of :variable_name where variable_name is a `ysqlsh` variable name are replaced by the variable's value. Also, appearances of `:'variable_name'` are replaced by the variable's value suitably quoted to become a single shell command argument. (The latter form is almost always preferable, unless you are very sure of what is in the variable.) Because carriage return and line feed characters cannot be safely quoted on all platforms, the `:'variable_name'` form prints an error message and doesn't substitute the variable value when such characters appear in the value.

Some commands take an SQL identifier (such as a table name) as argument. These arguments follow the syntax rules of SQL: Unquoted letters are forced to lowercase, while double quotes (`"`) protect letters from case conversion and allow incorporation of whitespace into the identifier. In double quotes, paired double quotes reduce to a single double quote in the resulting name. For example, `FOO"BAR"BAZ` is interpreted as `fooBARbaz`, and `"A weird"" name"` becomes `A weird" name`.

Parsing for arguments stops at the end of the line, or when another unquoted backslash is found. An unquoted backslash is taken as the beginning of a new meta-command. The special sequence `\\` (two backslashes) marks the end of arguments and continues parsing SQL commands, if any. That way SQL statements and `ysqlsh` commands can be freely mixed on a line. But in any case, the arguments of a meta-command cannot continue beyond the end of the line.

Many of the meta-commands act on the current query buffer. This buffer holds whatever SQL statement text has been typed but not yet sent to the server for execution. This includes previous input lines as well as any text appearing before the meta-command on the same line.

### Reference

The following meta-commands are available.

##### \a

If the current table output format is unaligned, it is switched to aligned. If it isn't unaligned, it is set to unaligned. This command is kept for backwards compatibility. See [\pset](#pset-option-value) for a more general solution.

##### \c, \connect [ -reuse-previous=on|off ] [ *dbname* [ *username* ] [ *host* ] [ *port* ] | *conninfo* ]

Establishes a new connection to a YugabyteDB server. The connection parameters to use can be specified either using a positional syntax, or using *conninfo* connection strings.

Where the command omits *dname*, *user*, *host*, or *port*, the new connection can reuse values from the previous connection. By default, values from the previous connection are reused except when processing a *conninfo* string. Passing a first argument of `-reuse-previous=on` or `-reuse-previous=off` overrides that default. When the command neither specifies nor reuses a particular parameter, the `libpq` default is used. Specifying any of *dbname*, *username*, *host*, or *port* as `-` is equivalent to omitting that parameter.

If the new connection is successfully made, the previous connection is closed. If the connection attempt failed (wrong user name, access denied, etc.), the previous connection is only kept if `ysqlsh` is in interactive mode. When executing a non-interactive script, processing immediately stops with an error. This distinction was chosen as a user convenience against typos on the one hand, and a safety mechanism that scripts aren't accidentally acting on the wrong database on the other hand.

Examples:

```plpgsql
=> \c mydb myuser host.dom 6432
=> \c service=foo
=> \c "host=localhost port=5432 dbname=mydb connect_timeout=10 sslmode=disable"
=> \c postgresql://tom@localhost/mydb?application_name=myapp
\C [ title ]
```

Sets the title of any tables being printed as the result of a query or unset any such title. This command is equivalent to [\pset title](#title-or-c). (The name of this command derives from "caption", as it was previously only used to set the caption in an HTML table.)

##### \cd [ *directory* ]

Changes the current working directory to *directory*. Without argument, changes to the current user's home directory.

{{< note title="Tip" >}}

To print your current working directory, use `\! pwd`.

{{< /note >}}

##### \conninfo

Outputs information about the current database connection, including database, user, host, and port.

##### \copy { *table* [ ( *column_list* ) ] | ( *query* ) } { from | to } { '*filename*' | program '*command*' | stdin | stdout | pstdin | pstdout } [ \[ with \] ( *option* [, ...] ) ]

Performs a frontend (client) copy. This is an operation that runs an SQL `COPY` statement, but instead of the server reading or writing the specified file, `ysqlsh` reads or writes the file and routes the data between the server and the local file system. This means that file accessibility and privileges are those of the local user, not the server, and no SQL superuser privileges are required.

When program is specified, *command* is executed by `ysqlsh` and the data passed from or to *command* is routed between the server and the client. Again, the execution privileges are those of the local user, not the server, and no SQL superuser privileges are required.

For `\copy ... from stdin`, data rows are read from the same source that issued the command, continuing until `\.` is read or the stream reaches EOF. This option is useful for populating tables in-line in a SQL script file. For `\copy ... to stdout`, output is sent to the same place as `ysqlsh` command output, and the COPY count command status isn't printed (as it might be confused with a data row). To read or write `ysqlsh`'s standard input or output, regardless of the current command source or `\o` option, write from `pstdin` or to `pstdout`.

The syntax of this command is similar to that of the SQL `COPY` statement. All options other than the data source or destination are as specified for `COPY`. Because of this, special parsing rules apply to the `\copy` meta-command. Unlike most other meta-commands, the entire remainder of the line is always taken to be the arguments of `\copy`, and neither variable interpolation nor backquote expansion are performed in the arguments.

{{< note title="Tip" >}}

Another way to obtain the same result as `\copy ... to` is to use the SQL [COPY ... TO STDOUT](../../api/ysql/the-sql-language/statements/cmd_copy) statement and terminate it with `\g *filename*` or `\g |*program*`. Unlike `\copy`, this method allows the command to span multiple lines; also, variable interpolation and backquote expansion can be used.

{{< /note >}}

{{< note title="Tip" >}}

These operations aren't as efficient as the SQL `COPY` statement with a file or program data source or destination, because all data must pass through the client-server connection. For large amounts of data, the SQL `COPY` statement might be preferable.

{{< /note >}}

##### \copyright

Shows the copyright and distribution terms of PostgreSQL, which YugabyteDB is derived from.

##### \crosstabview [ *colV* [ *colH* [ *colD* [ *sortcolH* ] ] ] ]

Executes the current query buffer (like `\g`) and shows the results in a crosstab grid. The query must return at least three columns. The output column identified by *colV* becomes a vertical header and the output column identified by *colH* becomes a horizontal header. *colD* identifies the output column to display in the grid. *sortcolH* identifies an optional sort column for the horizontal header.

Each column specification can be a column number (starting at `1`) or a column name. The usual SQL case folding and quoting rules apply to column names. If omitted, *colV* is taken as column 1 and *colH* as column 2. *colH* must differ from *colV*. If *colD* isn't specified, then there must be exactly three columns in the query result, and the column that is neither *colV* nor *colH* is taken to be *colD*.

The vertical header, displayed as the leftmost column, contains the values found in column *colV*, in the same order as in the query results, but with duplicates removed.

The horizontal header, displayed as the first row, contains the values found in column *colH*, with duplicates removed. By default, these appear in the same order as in the query results. But if the optional *sortcolH* argument is given, it identifies a column whose values must be integer numbers, and the values from *colH* appear in the horizontal header sorted according to the corresponding *sortcolH* values.

Inside the crosstab grid, for each distinct value `x` of *colH* and each distinct value `y` of *colV*, the cell located at the intersection (`x,y`) contains the value of the *colD* column in the query result row for which the value of *colH* is `x` and the value of *colV* is `y`. If there is no such row, the cell is empty. If there are multiple such rows, an error is reported.

##### \d[S+] [ [pattern](#patterns) ]

For each relation (table, view, materialized view, index, sequence, or foreign table) or composite type matching the *pattern*, show all columns, their types, and any special attributes such as `NOT NULL` or defaults. Associated indexes, constraints, rules, triggers, and tablegroups are also shown. For foreign tables, the associated foreign server is shown as well. ("Matching the pattern" is defined in [Patterns](#patterns).)

For some types of relation, `\d` shows additional information for each column: column values for sequences, indexed expressions for indexes, and foreign data wrapper options for foreign tables.

The command form `\d+` is identical, except that more information is displayed: any comments associated with the columns of the table are shown, as is the presence of OIDs in the table, the view definition if the relation is a view.

By default, only user-created objects are shown; supply a pattern or the `S` modifier to include system objects.

{{< note title="Note" >}}

If `\d` is used without a *pattern* argument, it is equivalent to `\dtvmsE`, which shows a list of all visible tables, views, materialized views, sequences, and foreign tables. This is purely a convenience measure.

{{< /note >}}

##### \da[S] [ [pattern](#patterns) ]

Lists aggregate functions, together with their return type and the data types they operate on. If *pattern* is specified, only aggregates whose names match the pattern are shown. By default, only user-created objects are shown; supply a pattern or the `S` modifier to include system objects.

##### \dA[+] [ [pattern](#patterns) ]

Lists access methods. If *pattern* is specified, only access methods whose names match the pattern are shown. If `+` is appended to the command name, each access method is listed with its associated handler function and description.

##### \dc[S+] [ [pattern](#patterns) ]

Lists conversions between character-set encodings. If *pattern* is specified, only conversions whose names match the pattern are listed. By default, only user-created objects are shown; supply a pattern or the `S` modifier to include system objects. If `+` is appended to the command name, each object is listed with its associated description.

##### \dC[+] [ [pattern](#patterns) ]

Lists type casts. If *pattern* is specified, only casts whose source or target types match the pattern are listed. If `+` is appended to the command name, each object is listed with its associated description.

##### \dd[S] [ [pattern](#patterns) ]

Shows the descriptions of objects of type constraint, operator class, operator family, rule, and trigger. All other comments may be viewed by the respective backslash commands for those object types.

`\dd` displays descriptions for objects matching the *pattern*, or of visible objects of the appropriate type if no argument is given. But in either case, only objects that have a description are listed. By default, only user-created objects are shown; supply a pattern or the `S` modifier to include system objects.

Descriptions for objects can be created with the SQL [COMMENT](../../api/ysql/the-sql-language/statements/ddl_comment) statement.

##### \dD[S+] [ [pattern](#patterns) ]

Lists domains. If *pattern* is specified, only domains whose names match the pattern are shown. By default, only user-created objects are shown; supply a pattern or the `S` modifier to include system objects. If `+` is appended to the command name, each object is listed with its associated permissions and description.

##### \ddp [ [pattern](#patterns) ]

Lists default access privilege settings. An entry is shown for each role (and schema, if applicable) for which the default privilege settings have been changed from the built-in defaults. If *pattern* is specified, only entries whose role name or schema name matches the pattern are listed.

The [ALTER DEFAULT PRIVILEGES](../../api/ysql/the-sql-language/statements/dcl_alter_default_privileges) statement is used to set default access privileges. The meaning of the privilege display is explained under [GRANT](../../api/ysql/the-sql-language/statements/dcl_grant).

##### \dE[S+], \di[S+], \dm[S+], \ds[S+], \dt[S+], \dv[S+] [ [pattern](#patterns) ]

In this group of commands, the letters `E`, `i`, `m`, `s`, `t`, and `v` stand for foreign table, index, materialized view, sequence, table, and view, respectively. You can specify any or all of these letters, in any order, to obtain a listing of objects of these types. For example, `\dit` lists indexes and tables. If `+` is appended to the command name, each object is listed with its physical size on disk and its associated description, if any. If [pattern](#patterns) is specified, only objects whose names match the pattern are listed. By default, only user-created objects are shown; supply a pattern or the `S` modifier to include system objects.

##### \des[+] [ [pattern](#patterns) ]

Lists foreign servers (mnemonic: "external servers"). If *pattern* is specified, only those servers whose name matches the pattern are listed. If the form \des+ is used, a full description of each server is shown, including the server's ACL, type, version, options, and description.

##### \det[+] [ [pattern](#patterns) ]

Lists foreign tables (mnemonic: "external tables"). If *pattern* is specified, only entries whose table name or schema name matches the pattern are listed. If the form `\det+` is used, generic options and the foreign table description are also displayed.

##### \deu[+] [ [pattern](#patterns) ]

Lists user mappings (mnemonic: "external users"). If *pattern* is specified, only those mappings whose user names match the pattern are listed. If the form `\deu+` is used, additional information about each mapping is shown.

{{< note title="Caution" >}}

`\deu+` might also display the user name and password of the remote user, so care should be taken not to disclose them.

{{< /note >}}

##### \dew[+] [ [pattern](#patterns) ]

Lists foreign-data wrappers (mnemonic: "external wrappers"). If *pattern* is specified, only those foreign-data wrappers whose name matches the pattern are listed. If the form `\dew+` is used, the ACL, options, and description of the foreign-data wrapper are also shown.

##### \df[antwS+] [ [pattern](#patterns) ]

Lists functions, together with their result data types, argument data types, and function types, which are classified as "agg" (aggregate), "normal", "trigger", or "window". To display only functions of specific types, add the corresponding letters `a`, `n`, `t`, or `w` to the command. If *pattern* is specified, only functions whose names match the pattern are shown. By default, only user-created objects are shown; supply a pattern or the `S` modifier to include system objects. If the form `\df+` is used, additional information about each function is shown, including volatility, parallel safety, owner, security classification, access privileges, language, source code, and description.

{{< note title="Tip" >}}

To look up functions taking arguments or returning values of a specific data type, use your pager's search capability to scroll through the `\df` output.

{{< /note >}}

##### \dF[+] [ [pattern](#patterns) ]

Lists text search configurations. If *pattern* is specified, only configurations whose names match the pattern are shown. If the form `\dF+` is used, a full description of each configuration is shown, including the underlying text search parser and the dictionary list for each parser token type.

##### \dFd[+] [ [pattern](#patterns) ]

Lists text search dictionaries. If *pattern* is specified, only dictionaries whose names match the pattern are shown. If the form `\dFd+` is used, additional information is shown about each selected dictionary, including the underlying text search template and the option values.

##### \dFp[+] [ [pattern](#patterns) ]

Lists text search parsers. If *pattern* is specified, only parsers whose names match the pattern are shown. If the form `\dFp+` is used, a full description of each parser is shown, including the underlying functions and the list of recognized token types.

##### \dFt[+] [ [pattern](#patterns) ]

Lists text search templates. If *pattern* is specified, only templates whose names match the pattern are shown. If the form `\dFt+` is used, additional information is shown about each template, including the underlying function names.

##### \dg[S+] [ [pattern](#patterns) ]

Lists database roles. (Because the concepts of "users" and "groups" have been unified into "roles", this command is now equivalent to [\du](#du-s-pattern-patterns).) By default, only user-created roles are shown; supply the `S` modifier to include system roles. If pattern is specified, only those roles whose names match the *pattern* are listed. If the form `\dg+` is used, additional information is shown about each role; currently this adds the comment for each role.

##### \dgr[+] [ [pattern](#patterns) ]

Lists database tablegroups. If the form `\dgr+` is used, additional information is shown about each tablegroup.

##### \dgrt[+] [ [pattern](#patterns) ]

Lists tables and indexes in database tablegroups matching the *pattern*. If the form `\dgrt+` is used, additional information is shown about each tablegroup and table.

##### \dl

This is an alias for [`\lo_list`](#lo-list), which shows a list of large objects.

##### \dL[S+] [ [pattern](#patterns) ]

Lists procedural languages. If *pattern* is specified, only languages whose names match the pattern are listed. By default, only user-created languages are shown; supply the *S* modifier to include system objects. If *+* is appended to the command name, each language is listed with its call handler, validator, access privileges, and whether it is a system object.

##### \dn[S+] [ [pattern](#patterns) ]

Lists schemas (namespaces). If *pattern* is specified, only schemas whose names match the pattern are listed. By default, only user-created objects are shown; supply a pattern or the `S` modifier to include system objects. If `+` is appended to the command name, each object is listed with its associated permissions and description, if any.

##### \do[S+] [ [pattern](#patterns) ]

Lists operators with their operand and result types. If *pattern* is specified, only operators whose names match the pattern are listed. By default, only user-created objects are shown; supply a pattern or the `S` modifier to include system objects. If `+` is appended to the command name, additional information about each operator is shown, currently just the name of the underlying function.

##### \dO[S+] [ [pattern](#patterns) ]

Lists collations. If *pattern* is specified, only collations whose names match the pattern are listed. By default, only user-created objects are shown; supply a pattern or the `S` modifier to include system objects. If `+` is appended to the command name, each collation is listed with its associated description, if any. Note that only collations usable with the current database's encoding are shown, so the results may vary in different databases of the same installation.

##### \dp [ [pattern](#patterns) ]

Lists tables, views, and sequences with their associated access privileges. If *pattern* is specified, only tables, views, and sequences whose names match the pattern are listed.

The [GRANT](../../api/ysql/the-sql-language/statements/dcl_grant)](../../api/ysql/the-sql-language/statements/dcl_grant) and [REVOKE](../../api/ysql/the-sql-language/statements/dcl_revoke) statements are used to set access privileges. The meaning of the privilege display is explained under [GRANT](../../api/ysql/the-sql-language/statements/dcl_grant).

##### \drds [ *role-pattern* [ *database-pattern* ] ]

Lists defined configuration settings. These settings can be role-specific, database-specific, or both. *role-pattern* and *database-pattern* are used to select specific roles and databases to list, respectively. If omitted, or if `*` is specified, all settings are listed, including those not role-specific or database-specific, respectively.

The [ALTER ROLE](../../api/ysql/the-sql-language/statements/dcl_alter_role) and [ALTER DATABASE](../../api/ysql/the-sql-language/statements/ddl_alter_db) statements are used to define per-role and per-database configuration settings.

##### \dRp[+] [ [pattern](#patterns) ]

Lists replication publications. If *pattern* is specified, only those publications whose names match the pattern are listed. If `+` is appended to the command name, the tables associated with each publication are shown as well.

##### \dRs[+] [ [pattern](#patterns) ]

Lists replication subscriptions. If *pattern* is specified, only those subscriptions whose names match the pattern are listed. If `+` is appended to the command name, additional properties of the subscriptions are shown.

##### \dT[S+] [ [pattern](#patterns) ]

Lists data types. If *pattern* is specified, only types whose names match the pattern are listed. If `+` is appended to the command name, each type is listed with its internal name and size, its allowed values if it is an enum type, and its associated permissions. By default, only user-created objects are shown; supply a pattern or the `S` modifier to include system objects.

##### \du[S+] [ [pattern](#patterns) ]

Lists database roles. (Because the concepts of "users" and "groups" have been unified into "roles", this command is now equivalent to [`\dg`](#dg-s-pattern-patterns).) By default, only user-created roles are shown; supply the `S` modifier to include system roles. If `pattern` is specified, only those roles whose names match the pattern are listed. If the form `\du+` is used, additional information is shown about each role; currently this adds the comment for each role.

##### \dx[+] [ [pattern](#patterns) ]

Lists installed extensions. If *pattern* is specified, only those extensions whose names match the pattern are listed. If the form `\dx+` is used, all the objects belonging to each matching extension are listed.

##### \dy[+] [ [pattern](#patterns) ]

Lists event triggers. If *pattern* is specified, only those event triggers whose names match the pattern are listed. If `+` is appended to the command name, each object is listed with its associated description.

##### \e, \edit [ *filename* ] [ *line_number* ]

If *filename* is specified, the file is edited; after the editor exits, the file's content is copied into the current query buffer. If no *filename* is given, the current query buffer is copied to a temporary file which is then edited in the same fashion. Or, if the current query buffer is empty, the most recently executed query is copied to a temporary file and edited in the same fashion.

The new contents of the query buffer are then re-parsed according to the normal rules of `ysqlsh`, treating the whole buffer as a single line. Any complete queries are immediately executed; that is, if the query buffer contains or ends with a semicolon, everything up to that point is executed. Whatever remains waits in the query buffer; type semicolon (`;`) or [\g](#g-filename-g-command) to send it, or [\r](#r-reset) to cancel it by clearing the query buffer. Treating the buffer as a single line primarily affects meta-commands: whatever is in the buffer after a meta-command is taken as arguments to the meta-command, even if it spans multiple lines. (Thus, you cannot make scripts using meta-commands this way. Use [\i](#i-filename-include-filename) for that.)

If a line number is specified, `ysqlsh` positions the cursor on the specified line of the file or query buffer. Note that if a single all-digits argument is given, `ysqlsh` assumes it is a line number, not a file name.

{{< note title="Tip" >}}

See [Environment variables](#environment-variables) for information on configuring and customizing your editor.

{{< /note >}}

##### \echo text [ ... ]

Prints the arguments to the standard output, separated by one space and followed by a newline. This can be used to intersperse information in the output of scripts. For example:

```shell
=> \echo `date`
Mon Dec 16 21:40:57 CEST 2019
```

If the first argument is an unquoted `-n`, the trailing newline isn't written.

{{< note title="Tip" >}}

If you use the `\o` command to redirect your query output, you might wish to use [\qecho](#qecho-text) instead of this command.

{{< /note >}}

##### \ef [ *function_description* [ *line_number* ] ]

This command fetches and edits the definition of the named function, in the form of a [CREATE OR REPLACE FUNCTION](../../api/ysql/the-sql-language/statements/ddl_create_function) statement. Editing is done in the same way as for [\edit](#e-edit-filename-line-number). After the editor exits, the updated command waits in the query buffer; type semicolon (`;`) or [\g](#g-filename-g-command) to send it, or [\r](#r-reset) to cancel.

The target function can be specified by name alone, or by name and arguments, for example, `foo(integer, text)`. The argument types must be given if there is more than one function of the same name.

If no function is specified, a blank `CREATE FUNCTION` template is presented for editing.

If a line number is specified, `ysqlsh` positions the cursor on the specified line of the function body. (Note that the function body typically doesn't begin on the first line of the file.)

Unlike most other meta-commands, the entire remainder of the line is always taken to be the arguments of `\ef`, and neither variable interpolation nor backquote expansion are performed in the arguments.

{{< note title="Tip" >}}

See [Environment variables](#environment-variables) for information on configuring and customizing your editor.

{{< /note >}}

##### \encoding [ *encoding* ]

Sets the client character set encoding. Without an argument, this command shows the current encoding.

##### \errverbose

Repeats the most recent server error message at maximum verbosity, as though [VERBOSITY](#verbosity) were set to `verbose` and [SHOW_CONTEXT](#show-context) were set to `always`.

##### \ev [ view_name [ line_number ] ]

This command fetches and edits the definition of the named view, in the form of a `CREATE OR REPLACE VIEW` statement. Editing is done in the same way as for \edit. After the editor exits, the updated command waits in the query buffer; type semicolon (`;`) or [\g](#g-filename-g-command) to send it, or [\r](#r-reset) to cancel.

If no view is specified, a blank `CREATE VIEW` template is presented for editing.

If a line number is specified, `ysqlsh` positions the cursor on the specified line of the view definition.

Unlike most other meta-commands, the entire remainder of the line is always taken to be the arguments of `\ev`, and neither variable interpolation nor backquote expansion are performed in the arguments.

##### \f [ string ]

Sets the field separator for unaligned query output. The default is the vertical bar (`|`). It is equivalent to [\pset fieldsep](#pset-option-value).

##### \g [ filename ], \g [ |command ]

Sends the current query buffer to the server for execution. If an argument is given, the query's output is written to the named file *filename* or piped to the given shell command *command*, instead of displaying it as usual. The file or command is written to only if the query successfully returns zero or more tuples, not if the query fails or is a non-data-returning SQL statement.

If the current query buffer is empty, the most recently sent query is re-executed instead. Except for that behavior, `\g` without an argument is essentially equivalent to a semicolon. A `\g` with argument is a "one-shot" alternative to the [\o](#o-out-filename-command) command.

If the argument begins with `|`, then the entire remainder of the line is taken to be the *command* to execute, and neither variable interpolation nor backquote expansion are performed in it. The rest of the line is passed literally to the shell.

##### \gexec

Sends the current query buffer to the server, then treats each column of each row of the query's output (if any) as a SQL statement to be executed. For example, to create an index on each column of `my_table`:

```plpgsql
=> SELECT format('create index on my_table(%I)', attname)
-> FROM pg_attribute
-> WHERE attrelid = 'my_table'::regclass AND attnum > 0
-> ORDER BY attnum
-> \gexec
CREATE INDEX
CREATE INDEX
CREATE INDEX
CREATE INDEX
```

The generated queries are executed in the order in which the rows are returned, and left-to-right in each row if there is more than one column. `NULL` fields are ignored. The generated queries are sent literally to the server for processing, so they cannot be `ysqlsh` meta-commands nor contain `ysqlsh` variable references. If any individual query fails, execution of the remaining queries continues unless `ON_ERROR_STOP` is set. Execution of each query is subject to `ECHO` processing. (Setting [`ECHO`](#echo) to `all` or `queries` is often advisable when using `\gexec`.) Query logging, single-step mode, timing, and other query execution features apply to each generated query as well.

If the current query buffer is empty, the most recently sent query is re-executed instead.

##### \gset [ prefix ]

Sends the current query buffer to the server and stores the query's output into `ysqlsh` variables (see [Variables](#variables)). The query to be executed must return exactly one row. Each column of the row is stored into a separate variable, named the same as the column. For example:

```plpgsql
=> SELECT 'hello' AS var1, 10 AS var2
-> \gset
=> \echo :var1 :var2
hello 10
```

If you specify a *prefix*, that string is prepended to the query's column names to create the variable names to use:

```plpgsql
=> SELECT 'hello' AS var1, 10 AS var2
-> \gset result_
=> \echo :result_var1 :result_var2
hello 10
```

If a column result is *NULL*, the corresponding variable is unset rather than being set.

If the query fails or doesn't return one row, no variables are changed.

If the current query buffer is empty, the most recently sent query is re-executed instead.

##### \gx [ filename ], \gx [ |command ]

`\gx` is equivalent to `\g,` but forces expanded output mode for this query. See `\x`.

##### \h or \help [ command ]

Gives syntax help on the specified SQL statement. If command isn't specified, then `ysqlsh` lists all the commands for which syntax help is available. If command is an asterisk (`*`), then syntax help on all SQL statements is shown.

Unlike most other meta-commands, the entire remainder of the line is always taken to be the arguments of `\help`, and neither variable interpolation nor backquote expansion are performed in the arguments.

{{< note title="Note" >}}

To simplify typing, commands that consists of several words don't have to be quoted. Thus, it is fine to type `\help alter table`.

{{< /note >}}

##### \H, \html

Turns on HTML query output format. If the HTML format is already on, it is switched back to the default aligned text format. This command is for compatibility and convenience, but see [\pset](#pset-option-value) about setting other output options.

##### \i *filename*, \include *filename*

Reads input from the file *filename* and executes it as though it had been typed on the keyboard.

If filename is `-` (hyphen), then standard input is read until an `EOF` indication or `\q` meta-command. This can be used to intersperse interactive input with input from files. Note that [Readline](#command-line-editing) behavior is used only if it is active at the outermost level.

{{< note title="Note" >}}

If you want to see the lines on the screen as they are read, you must set the variable `ECHO` to `all`.

{{< /note >}}

##### \if expression, \elif expression, \else, \endif

This group of commands implements nestable conditional blocks. A conditional block must begin with an `\if` and end with an `\endif`. In between there may be any number of `\elif` clauses, which may optionally be followed by a single `\else` clause. Ordinary queries and other types of backslash commands may (and usually do) appear between the commands forming a conditional block.

The `\if` and `\elif` commands read their arguments and evaluate them as a Boolean expression. If the expression yields true then processing continues normally; otherwise, lines are skipped until a matching `\elif`, `\else`, or `\endif` is reached. Once an `\if` or `\elif` test has succeeded, the arguments of later `\elif` commands in the same block aren't evaluated but are treated as false. Lines following an `\else` are processed only if no earlier matching `\if` or `\elif` succeeded.

The expression argument of an `\if` or `\elif` command is subject to variable interpolation and backquote expansion, just like any other backslash command argument. After that it is evaluated like the value of an `on` or `off` option variable. So a valid value is any unambiguous case-insensitive match for one of: `true`, `false`, `1`, `0`, `on`, `off`, `yes`, `no`. For example, `t`, `T`, and `tR` are all considered to be `true`.

Expressions that don't properly evaluate to `true` or `false` generate a warning and are treated as `false`.

Lines being skipped are parsed normally to identify queries and backslash commands, but queries aren't sent to the server, and backslash commands other than conditionals (`\if`, `\elif`, `\else`, `\endif`) are ignored. Conditional commands are checked only for valid nesting. Variable references in skipped lines aren't expanded, and backquote expansion isn't performed either.

All the backslash commands of a given conditional block must appear in the same source file. If EOF is reached on the main input file or an `\include`-ed file before all local `\if`-blocks have been closed, then `ysqlsh` raises an error.

Here is an example that checks for the existence of two separate records in the database and stores the results in separate `ysqlsh` variables:

```sql
SELECT
    EXISTS(SELECT 1 FROM customer WHERE customer_id = 123) as is_customer,
    EXISTS(SELECT 1 FROM employee WHERE employee_id = 456) as is_employee
\gset
\if :is_customer
    SELECT * FROM customer WHERE customer_id = 123;
\elif :is_employee
    \echo 'is not a customer but is an employee'
    SELECT * FROM employee WHERE employee_id = 456;
\else
    \if yes
        \echo 'not a customer or employee'
    \else
        \echo 'this will never print'
    \endif
\endif
```

##### \ir | \include_relative *filename*

The `\ir` command is similar to `\i`, but resolves relative file names differently. When executing in interactive mode, the two commands behave identically. However, when invoked from a script, `\ir` interprets file names relative to the directory in which the script is located, rather than the current working directory.

##### \l[+] | \list[+] [ [pattern](#patterns) ]

List the databases in the server and show their names, owners, character set encodings, and access privileges. If *pattern* is specified, only databases whose names match the pattern are listed. If `+` is appended to the command name, database sizes, and descriptions are also displayed. (Size information is only available for databases that the current user can connect to.)

##### \lo_export *loid* *filename*

Reads the large object with OID *loid* from the database and writes it to *filename*. Note that this is subtly different from the server function `lo_export`, which acts with the permissions of the user that the database server runs as and on the server's filesystem.

{{< note title="Tip" >}}

Use [\lo_list](#lo-list) to find out the large object's OID.

{{< /note >}}

##### \lo_import *filename* [ *comment* ]

Stores the file into a YugabyteDB large object. Optionally, it associates the given comment with the object. Example:

```sql
foo=> \lo_import '/home/peter/pictures/photo.xcf' 'a picture of me'
lo_import 152801
```

The response indicates that the large object received object ID `152801`, which can be used to access the newly-created large object in the future. For the sake of readability, it is recommended to always associate a human-readable comment with every object. Both OIDs and comments can be viewed with the [\lo_list](#lo-list) command.

Note that this command is subtly different from the server-side `lo_import` because it acts as the local user on the local filesystem, rather than the server's user and filesystem.

##### \lo_list

Shows a list of all YugabyteDB large objects currently stored in the database, along with any comments provided for them.

##### \lo_unlink *loid*

Deletes the large object with OID *loid* from the database.

{{< note title="Tip" >}}

To find out the large object's OID, use [\lo_list](#lo-list).

{{< /note >}}

##### \o | \out [ *filename* | |*command* ]

Arranges to save future query results to the file *filename* or pipe future results to the shell command *command*. If no argument is specified, the query output is reset to the standard output.

If the argument begins with `|`, then the entire remainder of the line is taken to be the *command* to execute, and neither variable interpolation nor backquote expansion are performed in it. The rest of the line is passed literally to the shell.

"Query results" includes all tables, command responses, and notices obtained from the database server, as well as output of various backslash commands that query the database (such as `\d`); but not error messages.

{{< note title="Tip" >}}

To intersperse text output in between query results, use `\qecho`.

{{< /note >}}

##### \p, \print

Print the current query buffer to the standard output. If the current query buffer is empty, the most recently executed query is printed instead.

##### \password [* username* ]

Changes the password of the specified user (by default, the current user). This command prompts for the new password, encrypts it, and sends it to the server as an [ALTER ROLE](../../api/ysql/the-sql-language/statements/dcl_alter_role) statement. This ensures that the new password doesn't appear in clear text in the command history, the server log, or elsewhere.

##### \prompt [ *text* ] *name*

Prompts the user to supply text, which is assigned to the variable *name*. An optional prompt string, *text*, can be specified. (For multi-word prompts, surround the text with single quotes.)

By default, `\prompt` uses the terminal for input and output. However, if the `-f` command line switch was used, `\prompt` uses standard input and standard output.

##### \pset [ *option* [ *value* ] ]

This command sets options affecting the output of query result tables. *option* indicates which option is to be set. The semantics of value vary depending on the selected option. For some options, omitting value causes the option to be toggled or unset, as described under the particular option. If no such behavior is mentioned, then omitting value just results in the current setting being displayed.

`\pset` without any arguments displays the current status of all printing options.

The *options* are defined in [pset options](#pset-options).

Illustrations of how these different formats look can be seen in the [Examples](#examples) section.

{{< note title="Tip" >}}

There are various shortcut commands for [`\pset`](#pset-option-value). See `\a`, `\C`, `\f`, `\H`, `\t`, `\T`, and `\x`.

{{< /note >}}

##### \q, \quit

Quits the `ysqlsh` program. In a script file, only execution of that script is terminated.

##### \qecho *text* [ ... ]

This command is identical to `\echo` except that the output is written to the query output channel, as set by `\o`.

##### \r, \reset

Resets (clears) the query buffer.

##### \s [ *filename* ]

Print `ysqlsh`'s command line history to *filename8. If filename is omitted, the history is written to the standard output (using the pager if appropriate). This command isn't available if `ysqlsh` was built without [Readline](#command-line-editing) support.

##### \set [ *name* [ *value* [ ... ] ] ]

Sets the `ysqlsh` variable *name* to *value*, or if more than one value is given, to the concatenation of all of them. If only one argument is given, the variable is set to an empty-string value. To unset a variable, use the `\unset` command.

`\set` without any arguments displays the names and values of all currently-set `ysqlsh` variables.

Valid variable names can contain letters, digits, and underscores. Variable names are case-sensitive. Certain variables are special, in that they control `ysqlsh`'s behavior or are automatically set to reflect connection state.

These variables are documented in [Variables](#variables).

{{< note title="Note" >}}

This command is unrelated to the SQL `SET` statement.

{{< /note >}}

##### \setenv *name* [ *value* ]

Sets the [environment variable](#environment-variables) *name* to *value*, or if *value* isn't supplied, un-sets the environment variable. Example:

```sql
testdb=> \setenv PAGER less
testdb=> \setenv LESS -imx4F
```

##### \sf[+] function_description

This command fetches and shows the definition of the named function, in the form of a [`CREATE OR REPLACE FUNCTION`](../../api/ysql/the-sql-language/statements/ddl_create_function) statement. The definition is printed to the current query output channel, as set by `\o`.

The target function can be specified by name alone, or by name and arguments, for example, `foo(integer, text)`. The argument types must be given if there is more than one function of the same name.

If `+` is appended to the command name, then the output lines are numbered, with the first line of the function body being line `1`.

Unlike most other meta-commands, the entire remainder of the line is always taken to be the arguments of `\sf`, and neither variable interpolation nor backquote expansion are performed in the arguments.

##### \sv[+] *view_name*

This command fetches and shows the definition of the named view, in the form of a [CREATE OR REPLACE VIEW](../../api/ysql/the-sql-language/statements/ddl_create_view) statement. The definition is printed to the current query output channel, as set by `\o`.

If `+` is appended to the command name, then the output lines are numbered from `1`.

Unlike most other meta-commands, the entire remainder of the line is always taken to be the arguments of `\sv`, and neither variable interpolation nor backquote expansion are performed in the arguments.

##### \t

Toggles the display of output column name headings and row count footer. This command is equivalent to [\pset tuples_only](#tuples-only-or-t) and is provided for convenience.

##### \T *table_options*

Specifies attributes to be placed in the `table` tag in HTML output format. This command is equivalent to [\pset tableattr <table_options>](#tableattr-or-t).

##### \timing [ on | off ]

With a parameter, turns displaying of how long each SQL statement takes `on` or `off`. Without a parameter, toggles the display between `on` and `off`. The display is in milliseconds; intervals longer than 1 second are also shown in minutes:seconds format, with hours and days fields added if needed.

##### \unset *name*

Un-sets (deletes) the `ysqlsh` variable *name*.

Most variables that control `ysqlsh`'s behavior cannot be unset; instead, an `\unset` command is interpreted as setting them to their default values. See [Variables](#variables) below.

##### \w | \write *filename* | |*command*

Writes the current query buffer to the file *filename* or pipes it to the shell command *command*. If the current query buffer is empty, the most recently executed query is written instead.

If the argument begins with `|`, then the entire remainder of the line is taken to be the command to execute, and neither variable interpolation nor backquote expansion are performed in it. The rest of the line is passed literally to the shell.

##### \watch [ *seconds* ]

Repeatedly execute the current query buffer (as [`\g`](#g-filename-g-command) does) until interrupted or the query fails. Wait the specified number of seconds (default `2`) between executions. Each query result is displayed with a header that includes the [\pset title string](#title-or-c) (if any), the time as of query start, and the delay interval.

If the current query buffer is empty, the most recently sent query is re-executed instead.

##### \x [ on | off | auto ]

Sets or toggles expanded table formatting mode. As such, it is equivalent to [\pset expanded](#expanded-or-x).

##### \z [ [pattern](#patterns) ]

Lists tables, views and sequences with their associated access privileges. If a *pattern* is specified, only tables, views, and sequences whose names match the pattern are listed.

This is an alias for [\dp](#dp-pattern-patterns) ("display privileges").

##### \\! [ *command* ]

With no argument, escapes to a sub-shell; `ysqlsh` resumes when the sub-shell exits. With an argument, executes the shell command *command*.

Unlike most other meta-commands, the entire remainder of the line is always taken to be the arguments of `\!`, and neither variable interpolation nor backquote expansion are performed in the arguments. The rest of the line is passed literally to the shell.

##### \\? [ *topic* ]

Shows help information. The optional *topic* parameter (defaulting to `commands`) selects which part of `ysqlsh` is explained: `commands` describes psql's backslash commands; `options` describes the command-line options that can be passed to psql; and `variables` shows help about `ysqlsh` configuration variables.

### Patterns

The various `\d` commands accept a *pattern* parameter to specify the object names to be displayed. In the simplest case, a pattern is just the exact name of the object. The characters in a pattern are normally folded to lower case, just as in SQL names; for example, `\dt FOO` displays the table named `foo`. As in SQL names, placing double quotes around a pattern stops folding to lower case. Should you need to include an actual double quote character in a pattern, write it as a pair of double quotes in a double-quote sequence; again this is in accord with the rules for SQL quoted identifiers. For example, `\dt "FOO""BAR"` displays the table named `FOO"BAR` (not `foo"bar`). Unlike the normal rules for SQL names, you can put double quotes around just part of a pattern; for instance `\dt FOO"FOO"BAR` displays the table named `fooFOObar`.

Whenever the *pattern* parameter is omitted completely, the `\d` commands display all objects that are visible in the current schema search path — this is equivalent to using `*` as the pattern. (An object is said to be visible if its containing schema is in the search path and no object of the same kind and name appears earlier in the search path. This is equivalent to the statement that the object can be referenced by name without explicit schema qualification.) To see all objects in the database regardless of visibility, use `*.*` as the pattern.

In a pattern, `*` matches any sequence of characters (including no characters) and `?` matches any single character. (This notation is comparable to Unix shell file name patterns.) For example, `\dt int*` displays tables whose names begin with `int`. But in double quotes, `*` and `?` lose these special meanings and are just matched literally.

A pattern that contains a dot (`.`) is interpreted as a schema name pattern followed by an object name pattern. For example, `\dt foo*.*bar*` displays all tables whose table name includes bar that are in schemas whose schema name starts with foo. When no dot appears, then the pattern matches only objects that are visible in the current schema search path. Again, a dot in double quotes loses its special meaning and is matched literally.

Advanced users can use regular-expression notations such as character classes, for example `[0-9]` to match any digit. All regular expression special characters work as specified, except for `.` which is taken as a separator as mentioned above, `*` which is translated to the regular-expression notation `.*`, `?` which is translated to `.`, and `$` which is matched literally. You can emulate these pattern characters at need by writing `?` for `.`, (`R+|`) for `R*`, or (`R|`) for `R?`. `$` isn't needed as a regular-expression character because the pattern must match the whole name, unlike the usual interpretation of regular expressions (in other words, `$` is automatically appended to your pattern). Write `*` at the beginning and/or end if you don't wish the pattern to be anchored. Note that in double quotes, all regular expression special characters lose their special meanings and are matched literally. Also, the regular expression special characters are matched literally in operator name patterns (that is, the argument of `\do`).

### pset options

Various adjustable printing options are supported.

#### border

The *value* must be a number. In general, the higher the number, the more borders and lines the tables have, but details depend on the particular format. In HTML, this translates directly into the `border=...` attribute. In most other formats only values `0` (no border), `1` (internal dividing lines), and `2` (table frame) make sense, and values greater than `2` are treated as `border = 2`. The latex and latex-longtable formats additionally allow a value of `3` to add dividing lines between data rows.

#### columns

Sets the target width for the `wrapped` format, and also the width limit for determining whether output is wide enough to require the pager or switch to the vertical display in expanded auto mode. The default value `0` causes the target width to be controlled by the environment variable [COLUMNS](#columns-1), or the detected screen width if `COLUMNS` isn't set. In addition, if columns is `0`, then the `wrapped` format only affects screen output. If columns is nonzero, then file and pipe output is wrapped to that width as well.

#### expanded (or x)

If *value* is specified it must be either `on` or `off`, which enables or disables expanded mode, or `auto`. If *value* is omitted, the command toggles between the `on` and `off` settings. When expanded mode is enabled, query results are displayed in two columns; the column name is on the left, and the data on the right. This is useful if the data won't fit on the screen in the normal horizontal mode. In the `auto` setting, the expanded mode is used whenever the query output has more than one column and is wider than the screen; otherwise, the regular mode is used. The `auto` setting is only effective in the aligned and wrapped formats. In other formats, it always behaves as if the expanded mode is `off`.

#### fieldsep

Specifies the field separator to be used in unaligned output format. That way, one can create, for example, tab- or comma-separated output, which other programs might prefer. To set a tab as field separator, run the command [\pset fieldsep '\t'](#fieldsep). The default field separator is `|` (a vertical bar).

#### fieldsep_zero

Sets the field separator to use in unaligned output format to a zero byte.

#### footer

If a *value* is specified, it must be either `on` or `off`, which enables or disables display of the table footer (the (n rows) count). If the value is omitted, the command toggles footer display `on` or `off`.

#### format

Sets the output format to one of `unaligned`, `aligned`, `wrapped`, `html`, `asciidoc`, `latex` (uses `tabular`), `latex-longtable`, or `troff-ms`. Unique abbreviations are allowed.

`unaligned` format writes all columns of a row on one line, separated by the currently active field separator. Use this option to create output intended to be read in by other programs (for example, tab-separated or comma-separated format).

`aligned` format is the standard, human-readable, nicely formatted text output; this is the default.

`wrapped` format is like aligned but wraps wide data values across lines to make the output fit in the target column width. The target width is determined as described under the columns option. Note that `ysqlsh` doesn't attempt to wrap column header titles; therefore, `wrapped` format behaves the same as aligned if the total width needed for column headers exceeds the target.

The `html`, `asciidoc`, `latex`, `latex-longtable`, and `troff-ms` formats put out tables that are intended to be included in documents using the respective markup language. They aren't complete documents! This might not be necessary in HTML, but in LaTeX you must have a complete document wrapper. `latex-longtable` also requires the LaTeX longtable and booktabs packages.

#### linestyle

Sets the border line drawing style to one of `ascii` or `unicode`. Unique abbreviations are allowed. (That would mean one letter is enough.) The default setting is `ascii`. This option only affects the `aligned` and `wrapped` output formats.

`ascii` style uses plain ASCII characters. Newlines in data are shown using a `+` symbol in the right-hand margin. When the `wrapped` format wraps data from one line to the next without a newline character, a dot (`.`) is shown in the right-hand margin of the first line, and again in the left-hand margin of the following line.

`unicode` style uses Unicode box-drawing characters. Newlines in data are shown using a carriage return symbol in the right-hand margin. When the data is wrapped from one line to the next without a newline character, an ellipsis symbol is shown in the right-hand margin of the first line, and again in the left-hand margin of the following line.

When the `border` setting is greater than `0` (zero), the `linestyle` option also determines the characters with which the border lines are drawn. Plain ASCII characters work everywhere, but Unicode characters look nicer on displays that recognize them.

#### null

Sets the string to be printed in place of a null value. The default is to print nothing, which can be mistaken for an empty string. For example, one might prefer `\pset null '(null)'`.

#### numericlocale

If *value* is specified, it must be either `on` or `off`, which enables or disables display of a locale-specific character to separate groups of digits to the left of the decimal marker. If *value* is omitted, the command toggles between regular and locale-specific numeric output.

#### pager

Controls use of a pager program for query and `ysqlsh` help output. If the environment variable [PAGER](#pager-1) is set, the output is piped to the specified program. Otherwise, a platform-dependent default (such as `more`) is used.

When the `pager` option is `off`, the pager program isn't used. When the `pager` option is `on`, the pager is used when appropriate; that is, when the output is to a terminal and doesn't fit on the screen. The `pager` option can also be set to `always`, which causes the pager to be used for all terminal output regardless of whether it fits on the screen. `\pset pager` without a *value* toggles pager use `on` and `off`.

#### pager_min_lines

If `pager_min_lines` is set to a number greater than the page height, the pager program isn't called unless there are at least this many lines of output to show. The default setting is `0`.

#### recordsep

Specifies the record (line) separator to use in unaligned output format. The default is a newline character.

#### recordsep_zero

Sets the record separator to use in unaligned output format to a zero byte.

#### tableattr (or T)

In `HTML` format, this specifies attributes to be placed inside the `table` tag. This could, for example, be `cellpadding` or `bgcolor`. Note that you probably don't want to specify border here, as that is already taken care of by [`\pset border`](#border). If no value is given, the table attributes are unset.

In `latex-longtable` format, this controls the proportional width of each column containing a left-aligned data type. It is specified as a whitespace-separated list of values, for example, `'0.2 0.2 0.6'`. Unspecified output columns use the last specified value.

#### title (or C)

Sets the table title for any subsequently printed tables. This can be used to give your output descriptive tags. If no *value* is given, the title is unset.

#### tuples_only (or t)

If *value* is specified, it must be either `on` or `off`, which enables or disables tuples-only mode. If *value* is omitted, the command toggles between regular and tuples-only output. Regular output includes extra information such as column headers, titles, and various footers. In tuples-only mode, only actual table data is shown.

#### unicode_border_linestyle

Sets the border drawing style for the `unicode` line style to one of `single` or `double`.

#### unicode_column_linestyle

Sets the column drawing style for the `unicode` line style to one of `single` or `double`.

#### unicode_header_linestyle

Sets the header drawing style for the `unicode` line style to one of `single` or `double`.

### Examples

The first example shows how to spread a SQL statement over several lines of input. Notice the changing prompt:

```sql
testdb=> CREATE TABLE my_table (
testdb(>  first integer not null default 0,
testdb(>  second text)
testdb-> ;
CREATE TABLE
```

Now look at the table definition again:

```sql
testdb=> \d my_table
```

```output
              Table "public.my_table"
 Column |  Type   | Collation | Nullable | Default
--------+---------+-----------+----------+---------
 first  | integer |           | not null | 0
 second | text    |           |          |
```

To change the prompt to something more interesting:

```sql
testdb=> \set PROMPT1 '%n@%m %~%R%# '
```

```output
peter@localhost testdb=>
```

Assume you've filled the table with data and want to take a look at it:

```sql
peter@localhost testdb=> SELECT * FROM my_table;
```

```output
 first | second
-------+--------
     1 | one
     2 | two
     3 | three
     4 | four
(4 rows)
```

You can display tables in different ways by using the [`\pset`](#pset-option-value) command:

```sql
peter@localhost testdb=> \pset border 2
```

```output
Border style is 2.
```

```sql
peter@localhost testdb=> SELECT * FROM my_table;
```

```output
+-------+--------+
| first | second |
+-------+--------+
|     1 | one    |
|     2 | two    |
|     3 | three  |
|     4 | four   |
+-------+--------+
(4 rows)
```

```sql
peter@localhost testdb=> \pset border 0
```

```output
Border style is 0.
```

```sql
peter@localhost testdb=> SELECT * FROM my_table;
```

```output
first second
----- ------
    1 one
    2 two
    3 three
    4 four
(4 rows)
```

```sql
peter@localhost testdb=> \pset border 1
```

```output
Border style is 1.
```

```sql
peter@localhost testdb=> \pset format unaligned
```

```output
Output format is unaligned.
```

```sql
peter@localhost testdb=> \pset fieldsep ","
```

```output
Field separator is ",".
```

```sql
peter@localhost testdb=> \pset tuples_only
```

```output
Showing only tuples.
```

```sql
peter@localhost testdb=> SELECT second, first FROM my_table;
```

```output
one,1
two,2
three,3
four,4
```

Alternatively, use the short commands:

```sql
peter@localhost testdb=> \a \t \x
```

```output
Output format is aligned.
Tuples only is off.
Expanded display is on.
```

```sql
peter@localhost testdb=> SELECT * FROM my_table;
```

```output
-[ RECORD 1 ]-
first  | 1
second | one
-[ RECORD 2 ]-
first  | 2
second | two
-[ RECORD 3 ]-
first  | 3
second | three
-[ RECORD 4 ]-
first  | 4
second | four
```

When suitable, query results can be shown in a crosstab representation with the `\crosstabview` command:

```sql
testdb=> SELECT first, second, first > 2 AS gt2 FROM my_table;
```

```output
 first | second | gt2
-------+--------+-----
     1 | one    | f
     2 | two    | f
     3 | three  | t
     4 | four   | t
(4 rows)
```

```sql
testdb=> \crosstabview first second
```

```output
 first | one | two | three | four
-------+-----+-----+-------+------
     1 | f   |     |       |
     2 |     | f   |       |
     3 |     |     | t     |
     4 |     |     |       | t
(4 rows)
```

This second example shows a multiplication table with rows sorted in reverse numerical order and columns with an independent, ascending numerical order.

```sql
testdb=> SELECT t1.first as "A", t2.first+100 AS "B", t1.first*(t2.first+100) as "AxB",
testdb(> row_number() over(order by t2.first) AS ord
testdb(> FROM my_table t1 CROSS JOIN my_table t2 ORDER BY 1 DESC
testdb(> \crosstabview "A" "B" "AxB" ord
```

```output
 A | 101 | 102 | 103 | 104
---+-----+-----+-----+-----
 4 | 404 | 408 | 412 | 416
 3 | 303 | 306 | 309 | 312
 2 | 202 | 204 | 206 | 208
 1 | 101 | 102 | 103 | 104
(4 rows)
```

## Variables

`ysqlsh` provides variable substitution features similar to common Linux command shells. Variables are name-value pairs, where the value can be any string of any length. The name must consist of letters (including non-Latin letters), digits, and underscores.

To set a variable, use the meta-command [\set](#set-name-value). For example,

```sql
testdb=> \set foo bar
```

sets the variable `foo` to the value `bar`. To retrieve the content of the variable, precede the name with a colon, for example:

```sql
testdb=> \echo :foo
bar
```

This works in both regular SQL statements and meta-commands; there is more detail in [SQL Interpolation](#sql-interpolation) below.

If you call `\set` without a second argument, the variable is set to an empty-string value. To unset (or delete) a variable, use the command [\unset](#unset-name). To show the values of all variables, call `\set` without any argument.

{{< note title="Note" >}}

The arguments of `\set` are subject to the same substitution rules as with other commands. Thus you can construct interesting references such as `\set :foo 'something'` and get "soft links" or "variable variables" of Perl or PHP fame, respectively. Unfortunately (or fortunately?), there is no way to do anything useful with these constructs. On the other hand, `\set bar :foo` is a perfectly valid way to copy a variable.

{{< /note >}}

A number of these variables are treated specially by `ysqlsh`. They represent certain option settings that can be changed at run time by altering the value of the variable, or in some cases represent changeable state of `ysqlsh`. By convention, all specially treated variables' names consist of all upper-case ASCII letters (and possibly digits and underscores). To ensure maximum compatibility in the future, avoid using such variable names for your own purposes.

Variables that control `ysqlsh`'s behavior generally can't be unset or set to invalid values. An `\unset` command is allowed but is interpreted as setting the variable to its default value. A `\set` command without a second argument is interpreted as setting the variable to `on`, for control variables that accept that value, and is rejected for others. Also, control variables that accept the values `on` and `off` also accept other common spellings of Boolean values, such as `true` and `false`.

The specially treated variables are:

##### AUTOCOMMIT

When `on` (the default), each SQL statement is automatically committed upon successful completion. To postpone commit in this mode, you must enter a [BEGIN](../../api/ysql/the-sql-language/statements/txn_begin/) or [START TRANSACTION](../../api/ysql/the-sql-language/statements/) SQL statement. When `off` or unset, SQL statements aren't committed until you explicitly issue `COMMIT` or `END` statements. The autocommit-off mode works by issuing an implicit `BEGIN` for you, just before any statement that isn't already in a transaction block and isn't itself a `BEGIN` or other transaction-control statement, nor a statement that cannot be executed inside a transaction block (such as `VACUUM`).

In autocommit-off mode, you must explicitly abandon any failed transaction by entering `ABORT` or `ROLLBACK`. Also, keep in mind that if you exit the session without committing, your work is lost.

The autocommit-on mode is YugabyteDB's traditional behavior, but autocommit-off is closer to the SQL specification. If you prefer autocommit-off, you might wish to set it in the system-wide [psqlrc file or your ~/.psqlrc file](#files).

##### COMP_KEYWORD_CASE

Determines which letter case to use when completing an SQL key word. If set to `lower` or `upper`, the completed word is in lowercase or uppercase, respectively. If set to `preserve-lower` or `preserve-upper` (the default), the completed word is in the case of the word already entered, but words being completed without anything entered are in lowercase or uppercase, respectively.

##### DBNAME

The name of the database you are currently connected to. This is set every time you connect to a database (including program start-up), but can be changed or unset.

##### ECHO

If set to `all`, all nonempty input lines are printed to standard output as they are read. (This doesn't apply to lines read interactively.) To select this behavior on program startup, use the switch `-a`. If set to `queries`, `ysqlsh` prints each query to standard output as it is sent to the server. The switch to select this behavior is `-e`. If set to `errors`, then only failed queries are displayed on standard error output. The switch for this behavior is `-b`. If set to `none` (the default), then no queries are displayed.

##### ECHO_HIDDEN

When this variable is set to `on` and a backslash command queries the database, the query is first shown. This feature helps you to study YugabyteDB internals and provide similar functionality in your own programs. (To select this behavior on program start-up, use the switch `-E`.) If you set this variable to the value noexec, the queries are just shown but aren't actually sent to the server and executed. The default value is `off`.

##### ENCODING

The current client character set encoding. This is set every time you connect to a database (including program start-up), and when you change the encoding with [\encoding](#encoding-encoding), but it can be changed or unset.

##### FETCH_COUNT

If this variable is set to an integer value greater than `0` (zero), the results of [SELECT](../../api/ysql/the-sql-language/statements/dml_select/) queries are fetched and displayed in groups of that many rows, rather than the default behavior of collecting the entire result set before display. Therefore, only a limited amount of memory is used, regardless of the size of the result set. Settings of `100` to `1000` are commonly used when enabling this feature. Keep in mind that when using this feature, a query might fail after having already displayed some rows.

{{< note title="Tip" >}}

Although you can use any output format with this feature, the default `aligned` format tends to look bad because each group of `FETCH_COUNT` rows is formatted separately, leading to varying column widths across the row groups. The other output formats work better.

{{< /note >}}

##### HISTCONTROL

If this variable is set to `ignorespace`, lines which begin with a space aren't entered into the history list. If set to a value of `ignoredups`, lines matching the previous history line aren't entered. A value of `ignoreboth` combines the two options. If set to `none` (the default), all lines read in interactive mode are saved on the history list.

{{< note title="Note" >}}

This feature was shamelessly plagiarized from Bash.

{{< /note >}}

##### HISTFILE

The file name used to store the history list. If unset, the file name is taken from the [PSQL_HISTORY](#psql-history) environment variable. If that isn't set either, the default is [~/.psql_history](#psql-history-1). For example, putting:

```sql
\set HISTFILE ~/.psql_history- :DBNAME
```

in `~/.psqlrc` causes `ysqlsh` to maintain a separate history for each database.

{{< note title="Note" >}}

This feature was shamelessly plagiarized from Bash.

{{< /note >}}

##### HISTSIZE

The maximum number of commands to store in the command history (default is `500`). If set to a negative value, no limit is applied.

{{< note title="Note" >}}

This feature was shamelessly plagiarized from Bash.

{{< /note >}}

##### HOST

The database server host you are currently connected to. This is set every time you connect to a database (including program start-up), but can be changed or unset.

##### IGNOREEOF

If set to `1` or less, sending an EOF character (usually **Control+D**) to an interactive session of `ysqlsh` terminates the application. If set to a larger numeric value, that many consecutive EOF characters must be typed to make an interactive session terminate. If the variable is set to a non-numeric value, it is interpreted as `10`. The default is `0`.

{{< note title="Note" >}}

This feature was shamelessly plagiarized from Bash.

{{< /note >}}

##### LASTOID

The value of the last affected OID, as returned from an [`INSERT`](../../api/ysql/the-sql-language/statements/dml_insert) statement or [`\lo_import`](#lo-import-filename-comment) command. This variable is only guaranteed to be valid until after the result of the next SQL statement has been displayed.

##### ON_ERROR_ROLLBACK

When set to `on`, if a statement in a transaction block generates an error, the error is ignored and the transaction continues. When set to `interactive`, such errors are only ignored in interactive sessions, and not when reading script files. When set to `off` (the default), a statement in a transaction block that generates an error aborts the entire transaction. The error rollback mode works by issuing an implicit `SAVEPOINT` for you, just before each statement that is in a transaction block, and then rolling back to the savepoint if the command fails.

##### ON_ERROR_STOP

By default, command processing continues after an error. When this variable is set to on, processing instead stops immediately. In interactive mode, `ysqlsh` returns to the command prompt; otherwise, `ysqlsh` exits, returning error code `3` to distinguish from fatal error conditions, which are reported using error code `1`. In either case, any currently running scripts (the top-level script, if any, and any other scripts which it may have in invoked) are terminated immediately. If the top-level command string contained multiple SQL statements, processing stops with the current statement.

##### PORT

The database server port to which you are currently connected. This is set every time you connect to a database (including program start-up), but can be changed or unset.

##### PROMPT1 / PROMPT2 / PROMPT3

These specify what the prompts `ysqlsh` issues should look like. See [Prompting](#prompting) below.

##### QUIET

Setting this variable to `on` is equivalent to the command line option `-q`. It is probably not too useful in interactive mode.

##### SERVER_VERSION_NAME / SERVER_VERSION_NUM

The server's version number as a string, for example, `11.2-YB-2.0.7.0-b0`, and in numeric form, for example, `110002`. These are set every time you connect to a database (including program start-up), but can be changed or unset.

##### SHOW_CONTEXT

This variable can be set to the values `never`, `errors`, or `always` to control whether `CONTEXT` fields are displayed in messages from the server. The default is `errors` (meaning that context is shown in error messages, but not in notice or warning messages). This setting has no effect when [VERBOSITY](#verbosity) is set to `terse`. (See also [\errverbose](#errverbose), for use when you want a verbose version of the error you just got.)

##### SINGLELINE

Setting this variable to on is equivalent to the command line option `-S`.

##### SINGLESTEP

Setting this variable to on is equivalent to the command line option `-s`.

##### USER

The database user you are currently connected as. This is set every time you connect to a database (including program start-up), but can be changed or unset.

##### VERBOSITY

This variable can be set to the values `default`, `verbose`, or `terse` to control the verbosity of error reports. (See also [\errverbose](#errverbose), for use when you want a verbose version of the error you just got.)

##### VERSION / VERSION_NAME / VERSION_NUM

These variables are set at program start-up to reflect `ysqlsh`'s version, respectively as a verbose string, a short string (for example, `9.6.2`, `10.1`, or `11beta1`), and a number (for example, `90602` or `100001`). They can be changed or unset.

### SQL interpolation

A key feature of `ysqlsh` variables is that you can substitute ("interpolate") them into regular SQL statements, as well as the arguments of meta-commands. Furthermore, `ysqlsh` provides facilities for ensuring that variable values used as SQL literals and identifiers are properly quoted. The syntax for interpolating a value without any quoting is to prepend the variable name with a colon (`:`). For example,

```plpgsql
testdb=> \set foo 'my_table'
testdb=> SELECT * FROM :foo;
```

would query the table `my_table`. Note that this may be unsafe: the value of the variable is copied literally, so it can contain unbalanced quotes, or even backslash commands. You must make sure that it makes sense where you put it.

When a value is to be used as an SQL literal or identifier, it is safest to arrange for it to be quoted. To quote the value of a variable as an SQL literal, write a colon followed by the variable name in single quotes. To quote the value as an SQL identifier, write a colon followed by the variable name in double quotes. These constructs deal correctly with quotes and other special characters embedded in the variable value. The previous example would be more safely written this way:

```plpgsql
testdb=> \set foo 'my_table'
testdb=> SELECT * FROM :"foo";
```

Variable interpolation isn't performed in quoted SQL literals and identifiers. Therefore, a construction such as `':foo'` doesn't work to produce a quoted literal from a variable's value (and it would be unsafe if it did work, as it wouldn't correctly handle quotes embedded in the value).

One example use of this mechanism is to copy the contents of a file into a table column. First, load the file into a variable, and then interpolate the variable's value as a quoted string:

```plpgsql
testdb=> \set content `cat my_file.txt`
testdb=> INSERT INTO my_table VALUES (:'content');
```

(Note that this still won't work if `my_file.txt` contains `NUL` bytes. `ysqlsh` doesn't support embedded `NUL` bytes in variable values.)

Because colons can legally appear in SQL statements, an apparent attempt at interpolation (that is, `:name`, `:'name'`, or `:"name"`) isn't replaced unless the named variable is currently set. In any case, you can escape a colon with a backslash to protect it from substitution.

The colon syntax for variables is standard SQL for embedded query languages, such as ECPG. The colon syntaxes for array slices and type casts are YugabyteDB extensions, which can sometimes conflict with the standard usage. The colon-quote syntax for escaping a variable's value as an SQL literal or identifier is a `ysqlsh` extension.

## Environment variables

Use the following environment variables to configure and customize your editor. Use the [\setenv](#setenv-name-value) meta-command to set environment variables. `ysqlsh` also uses the environment variables supported by `libpq`.

##### COLUMNS

If [\pset columns](#columns) is zero (`0`), controls the width for the wrapped format and width for determining if wide output requires the pager or should be switched to the vertical format in expanded auto mode.

##### PAGER

If the query results don't fit on the screen, they are piped through this command. Typical values are `more` or `less`. The default is platform-dependent. Use of the pager can be disabled by setting `PAGER` to `empty`, or by using [pager-related options](#pager) of the `\pset` meta-command.

##### PGDATABASE, PGHOST, PGPORT, PGUSER

Default connection parameters.

##### PSQL_EDITOR, EDITOR, VISUAL

Editor used by the [\e](#e-edit-filename-line-number), [\ef](#ef-function-description-line-number), and [\ev](#ev-view-name-line-number) commands. These variables are examined in the order listed; the first that is set is used.

On Linux systems, the built-in default editor is `vi`.

##### PSQL_EDITOR_LINENUMBER_ARG

When [\e](#e-edit-filename-line-number), [\ef](#ef-function-description-line-number), and [\ev](#ev-view-name-line-number) is used with a line number argument, this variable specifies the command-line argument used to pass the starting line number to the user's editor. For editors such as `emacs` or `vi`, this is a plus (`+`) sign. Include a trailing space in the value of the variable if there needs to be space between the option name and the line number.

**Examples:**

```sh
PSQL_EDITOR_LINENUMBER_ARG='+'
PSQL_EDITOR_LINENUMBER_ARG='--line '
```

The default is `+` on Linux systems (corresponding to the default editor `vi`, and useful for many other common editors).

##### PSQL_HISTORY

Alternative location for the command history file. Tilde (`~`) expansion is performed.

##### PSQLRC

Alternative location of the user's `.psqlrc` file. Tilde (`~`) expansion is performed.

##### SHELL

Command executed by the `\!` command.

##### TMPDIR

Directory for storing temporary files. The default is `/tmp`.

## Prompting

You can customize the prompts `ysqlsh` issues. The variables [PROMPT1, PROMPT2, and PROMPT3](#prompt1-prompt2-prompt3) contain strings and special escape sequences that describe the appearance of the prompt.

Prompt 1 is the normal prompt that is issued when `ysqlsh` requests a new command.

Prompt 2 is issued when more input is expected during command entry, such as when the command wasn't terminated with a semicolon or a quote wasn't closed.

Prompt 3 is issued when you are running an SQL [COPY FROM STDIN](../../api/ysql/the-sql-language/statements/cmd_copy) statement and you need to type in a row value on the terminal.

`ysqlsh` prints the value of the selected prompt variable literally, except where it encounters a percent sign (`%`), in which case it substitutes other text as defined in [Prompt substitutions](#prompt-substitutions).

To insert a percent sign into your prompt, write `%%`.

The default prompts are `'%/%R%# '` for prompts 1 and 2, and `'>> '` for prompt 3.

{{< note title="Note" >}}

This feature was shamelessly plagiarized from `tcsh`.

{{< /note >}}

### Prompt substitutions

Defined substitutions are as follows.

##### %M

The full host name (with domain name) of the database server, or `[local]` if the connection is over a Unix domain socket, or `[local:/dir/name]`, if the Unix domain socket isn't at the compiled in default location.

##### %m

The host name of the database server, truncated at the first dot, or `[local]` if the connection is over a Unix domain socket.

##### %>

The port number at which the database server is listening.

##### %n

The database session user name. (The expansion of this value might change during a database session as the result of the [SET SESSION AUTHORIZATION](../../api/ysql/the-sql-language/statements/dcl_set_session_authorization/) statement.)

##### %/

The name of the current database.

##### %~

Like `%/`, but the output is `~` (tilde) if the database is your default database.

##### %#

If the session user is a database superuser, then a `#`, otherwise a `>`. (The expansion of this value might change during a database session as the result of the [SET SESSION AUTHORIZATION](../../api/ysql/the-sql-language/statements/dcl_set_session_authorization/) statement.)

##### %p

The process ID of the backend currently connected to.

##### %R

In prompt 1, normally `=`, but `@` if the session is in an inactive branch of a conditional block, or `^` if in single-line mode, or `!` if the session is disconnected from the database (which can happen if [\connect](#c-connect-reuse-previous-on-off-dbname-username-host-port-conninfo) fails).

In prompt 2, `%R` is replaced by a character that depends on why `ysqlsh` expects more input, as follows:

- `-` if the command wasn't terminated yet, but `*` if there is an unfinished `/* ... */` comment
- a single quote (`'`) if there is an unfinished quoted string
- a double quote (`"`) if there is an unfinished quoted identifier
- a dollar sign (`$`) if there is an unfinished dollar-quoted string
- `(` if there is an unmatched left parenthesis

In prompt 3, `%R` doesn't produce anything.

##### %x

Transaction status: an empty string when not in a transaction block, or `*` when in a transaction block, or `!` when in a failed transaction block, or `?` when the transaction state is indeterminate (for example, because there is no connection).

##### %l

The line number inside the current statement, starting from `1`.

##### %digits

The character with the indicated octal code is substituted.

##### %:name:

The value of the `ysqlsh` variable name. For details, see [Variables](#variables).

##### %\`*command*\`

The output of *command*, similar to ordinary "back-tick" substitution.

##### %[ ... %]

Prompts can contain terminal control characters. You can use these to, for example, change the color, background, or style of the prompt text, or change the title of the terminal window. For the line editing features of [Readline](#command-line-editing) to work properly, these non-printing control characters must be designated as invisible by enclosing them in `%[` and `%]`. Multiple pairs of these can occur in the prompt. For example:

```plpgsql
testdb=> \set PROMPT1 '%[%033[1;33;40m%]%n@%/%R%[%033[0m%]%# '
```

results in a boldfaced (`1;`) yellow-on-black (`33;40`) prompt on VT100-compatible, color-capable terminals.

### Command-line editing

`ysqlsh` supports the Readline library for convenient line editing and retrieval. The command history is automatically saved when `ysqlsh` exits and is reloaded when `ysqlsh` starts up. Tab-completion is also supported, although the completion logic makes no claim to be an SQL parser. The queries generated by tab-completion can also interfere with other SQL statements, for example, the [SET TRANSACTION ISOLATION LEVEL](../../api/ysql/the-sql-language/statements/txn_set) statement. If for some reason you don't like the tab completion, you can turn it off by putting this in a file named `.inputrc` in your home directory:

```sh
$if psql
set disable-completion on
$endif
```

(This is a feature of Readline, not `ysqlsh`. For details, see the Readline documentation.)

## Files

##### psqlrc and ~/.psqlrc

Unless it is passed an `-X` option, `ysqlsh` attempts to read and execute commands from the system-wide startup file (`psqlrc`) and then the user's personal startup file (`~/.psqlrc`), after connecting to the database but before accepting normal commands. These files can be used to set up the client and the server to taste, typically with [\set](#set-name-value) and `SET` statements.

The system-wide startup file is named `psqlrc` and is sought in the installation's "system configuration" directory, which is most reliably identified by running `pg_config --sysconfdir`. By default this directory is `../etc/` relative to the directory containing the PostgreSQL executables. The name of this directory can be set explicitly using the `PGSYSCONFDIR` environment variable.

The user's personal startup file is named `.psqlrc` and is sought in the invoking user's home directory. The location of the user's startup file can be set explicitly through the [PSQLRC](#psqlrc) environment variable.

Both the system-wide startup file and the user's personal startup file can be made `ysqlsh`-version-specific by appending a dash and the YugabyteDB major or minor release number to the file name, for example `~/.psqlrc-10.2` or `~/.psqlrc-10.2.5`. The most specific version-matching file is read in preference to a non-version-specific file.

##### .psql_history

The command-line history is stored in the file `~/.psql_history`.

The location of the history file can be set explicitly using the `ysqlsh` [HISTFILE](#histfile) variable or the [PSQL_HISTORY](#psql-history) environment variable.
