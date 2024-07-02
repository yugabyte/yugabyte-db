---
title: ysqlsh - YSQL shell for YugabyteDB
headerTitle: ysqlsh
linkTitle: ysqlsh
description: Use the YSQL shell (ysqlsh) to interact with YugabyteDB.
headcontent: Shell for interacting with the YugabyteDB YSQL API
menu:
  stable:
    identifier: ysqlsh
    parent: admin
    weight: 10
type: docs
---

## Overview

The YugabyteDB SQL shell ysqlsh provides a CLI for interacting with YugabyteDB using [YSQL](../../api/ysql/). It enables you to:

- interactively enter SQL queries and see the query results
- input from a file or the command line
- use [meta-commands](../ysqlsh-meta-commands/) for scripting and administration

### Installation

ysqlsh is installed with YugabyteDB and is located in the `bin` directory of the YugabyteDB home directory.

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
    $ curl -sSL https://downloads.yugabyte.com/get_clients.sh | bash
    ```

    If you have `wget`, you can use the following:

    ```sh
    wget -q -O - https://downloads.yugabyte.com/get_clients.sh | sh
    ```

ysqlsh works best with servers of the same or an older major version. [Meta-commands](../ysqlsh-meta-commands/) are particularly likely to fail if the server is a newer version than ysqlsh itself. The general functionality of running SQL statements and displaying query results should also work with servers of a newer major version, but this cannot be guaranteed in all cases.

If you are running multiple versions of YugabyteDB, use the newest version of ysqlsh to connect. You can keep and use the matching version of ysqlsh to use with each version of YugabyteDB, but in practice, this shouldn't be necessary.

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

ysqlsh returns the following to the shell on exit:

- `0` if it finished normally
- `1` if a fatal error of its own occurs (for example, `out of memory`, `file not found`)
- `2` if the connection to the server went bad and the session wasn't interactive
- `3` if an error occurred in a script and the variable [ON_ERROR_STOP](#on-error-stop) was set

## Using ysqlsh

### Connect to a database

{{< note title="YugabyteDB Aeon" >}}

For information on connecting to a YugabyteDB Aeon cluster using ysqlsh, refer to [Connect via client shells](../../yugabyte-cloud/cloud-connect/connect-client-shell/).

{{< /note >}}

To connect to a database, you need the following information:

- the name of your target database
- the host name and port number of the server
- the user name you want to connect as

You provide these parameters using the `-d`, `-h`, `-p`, and `-U` flags.

ysqlsh provides the following defaults for these values:

- If you omit the host name, ysqlsh connects to the compiled-in default of `127.0.0.1` or a Unix-domain socket to a server on the local host, or using TCP/IP to localhost on machines that don't have Unix-domain sockets.
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

If the connection can't be made for any reason (for example, insufficient privileges, server isn't running on the targeted host, etc.), ysqlsh [returns an error](#exit-status) and terminates.

If both standard input and standard output are a terminal, then `ysql` sets the client encoding to `auto`, which detects the appropriate client encoding from the locale settings (`LC_CTYPE` environment variable on Linux systems). If this doesn't work out as expected, the client encoding can be overridden using the environment variable `PGCLIENTENCODING`.

### Entering SQL statements

In normal operation, ysqlsh provides a [prompt](#prompting) with the name of the database to which ysqlsh is currently connected, followed by the string `=>`. For example:

```plpgsql
$ ysqlsh testdb
ysqlsh ()
Type "help" for help.

testdb=>
```

At the prompt, the user can type in SQL statements. Ordinarily, input lines are sent to the server when a command-terminating semicolon (`;`) is reached. An end of line doesn't terminate a statement. Thus, commands can be spread over several lines for clarity. If the statement was sent and executed without error, the results of the statement are displayed on the screen.

{{< note title="Note" >}}

If untrusted users have access to a database that hasn't adopted a secure schema usage pattern, begin your session by removing publicly-writable schemas from `search_path`. You can add `options=-csearch_path=` to the connection string or issue `SELECT pg_catalog.set_config('search_path', '', false)` before other SQL statements. This consideration isn't specific to ysqlsh; it applies to every interface for executing arbitrary SQL statements.

{{< /note >}}

{{< note title="Note" >}}

When using [\i](../ysqlsh-meta-commands/#i-filename-include-filename) to restore a YugabyteDB database, SQL script files generated by `ysql_dump` and `ysql_dumpall` issues the `SELECT pg_catalog.set_config('search_path', '', false)` statement before executing any other SQL statements.

{{< /note >}}

Whenever a statement is executed, ysqlsh also polls for asynchronous notification events generated by `LISTEN` and `NOTIFY`.

While C-style block comments are passed to the server for processing and removal, SQL-standard comments are removed by ysqlsh.

## Syntax

```sh
ysqlsh [ <option>...] [ <dbname> [ <username> ]]
```

### Default flags

When you open ysqlsh, the following default flags (aka flags) are set so that the user doesn't have to specify them.

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

Specifies that ysqlsh is to execute the given command string, *command*. This flag can be repeated and combined in any order with the `-f` flag. When either `-c` or `-f` is specified, ysqlsh doesn't read commands from standard input; instead it terminates after processing all the `-c` and `-f` flags in sequence.

The command (*command*) must be either a command string that is completely parsable by the server (that is, it contains no ysqlsh-specific features), or a single backslash (`\`) command. Thus, you cannot mix SQL and ysqlsh meta-commands in a `-c` flag. To achieve that, you could use repeated `-c` flags or pipe the string into ysqlsh, for example:

```plpgsql
ysqlsh -c '\x' -c 'SELECT * FROM foo;'
```

or

```plpgsql
echo '\x \\ SELECT * FROM foo;' | ./bin/ysqlsh
```

(`\\` is the separator meta-command.)

Each SQL statement string passed to `-c` is sent to the server as a single query. Because of this, the server executes it as a single transaction even if the string contains multiple SQL statements, unless there are explicit `BEGIN` and `COMMIT` statements included in the string to divide it into multiple transactions. Also, ysqlsh only prints the result of the last SQL statement in the string. This is different from the behavior when the same string is read from a file or fed to ysqlsh's standard input, because then ysqlsh sends each SQL statement separately.

Because of this behavior, putting more than one SQL statement in a single `-c` string often has unexpected results. It's better to use repeated `-c` commands or feed multiple commands to ysqlsh's standard input, either using `echo` as illustrated above, or using a shell here-document, for example:

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

Echo the actual queries generated by [\d](../ysqlsh-meta-commands/#d-s-pattern-patterns) and other meta-commands. You can use this to study the internal operations of ysqlsh. This is equivalent to setting the variable [ECHO_HIDDEN](#echo-hidden) to `on`.

##### -f *filename*, --file=*filename*

Read statements from the file *filename*, rather than standard input. This option can be repeated and combined in any order with the `-c` option. When either `-c` or `-f` is specified, ysqlsh doesn't read statements from standard input; instead it terminates after processing all the `-c` and `-f` options in sequence. Except for that, this option is largely equivalent to the meta-command [\i](../ysqlsh-meta-commands/#i-filename-include-filename).

If *filename* is `-` (hyphen), then standard input is read until an EOF indication or [\q](../ysqlsh-meta-commands/#q-quit) meta-command. This can be used to intersperse interactive input with input from files. Note however that [Readline](#command-line-editing) isn't used in this case (much as if `-n` had been specified).

Using this option is subtly different from writing `ysqlsh < <filename>`. In general, both do what you expect, but using `-f` enables some nice features such as error messages with line numbers. There is also a slight chance that using this option reduces the start-up overhead. On the other hand, the variant using the shell's input redirection is (in theory) guaranteed to yield exactly the same output you would have received had you entered everything by hand.

##### -F *separator*, --field-separator=*separator*

Use *separator* as the field separator for unaligned output. This is equivalent to `\pset fieldsep` or `\f`.

##### -h *hostname*, --host=*hostname*

Specifies the host name of the machine on which the server is running. If the value begins with a slash (`/`), it is used as the directory for the Unix-domain socket. Default value is `127.0.0.1`.

##### -H, --html

Turn on HTML tabular output. This is equivalent to [\pset format html](../ysqlsh-pset-options/#format) or the [\H](../ysqlsh-meta-commands/#h-html) command.

##### -l, --list

List all available databases, then exit. Other non-connection options are ignored. This is similar to the meta-command [`\list`](../ysqlsh-meta-commands/#l-list-pattern-patterns).

When this option is used, ysqlsh connects to the database `yugabyte`, unless a different database is named on the command line (flag `-d` or non-option argument, possibly using a service entry, but not using an environment variable).

##### -L *filename*, --log-file=*filename*

Write all query output into file *filename*, in addition to the normal output destination.

##### -n, --no-readline

Don't use [Readline](#command-line-editing) for line editing and don't use the command history. This can be used to turn off tab expansion when cutting and pasting.

##### -o *filename*, --output=*filename*

Put all query output into file *filename*. This is equivalent to the command `\o`.

##### -p *port*, --port=*port*

Specifies the TCP port or the local Unix-domain socket file extension on which the server is listening for connections. Defaults to the compiled-in value of `5433` unless the [PGPORT](#pgdatabase-pghost-pgport-pguser) environment variable is set.

##### -P *assignment* |--pset=*assignment*

Specifies printing options, in the style of [\pset](../ysqlsh-meta-commands/#pset-option-value). Note that here you have to separate name and value with an equal sign instead of a space. For example, to set the output format to LaTeX, you could write `-P format=latex`.

##### -q, --quiet

Specifies that ysqlsh should do its work quietly. By default, it prints welcome messages and various informational output. If this option is used, none of this happens. This is helpful with the `-c` option. This is equivalent to setting the variable [QUIET](#quiet) to `on`.

##### -R *separator*, --record-separator=*separator*

Use *separator* as the record separator for unaligned output. This is equivalent to [\pset recordsep](../ysqlsh-pset-options/#recordsep).

##### -s, --single-step

Run in single-step mode. That means the user is prompted before each command is sent to the server, with the option to cancel execution as well. Use this to debug scripts.

##### -S, --single-line

Runs in single-line mode where a newline terminates an SQL statement, as a semicolon does.

{{< note title="Note" >}}

This mode is provided for those who insist on it, but you aren't necessarily encouraged to use it. In particular, if you mix SQL statements and meta-commands on a line, the order of execution may not be clear to inexperienced users.

{{< /note >}}

##### -t, --tuples-only

Turn off printing of column names and result row count footers, etc. This is equivalent to `\t` or [\pset tuples_only](../ysqlsh-pset-options/#tuples-only-or-t).

##### -T *table_options*, --table-attr=*table_options*

Specifies options to be placed in the HTML `table` tag. For details, see [\pset tableattr](../ysqlsh-pset-options/#tableattr-or-t).

##### -U *username*, --username=*username*

Connect to the database as the user *username* instead of the default. (You must have permission to do so, of course.)

##### -v *assignment*, --set=*assignment*, --variable=*assignment*

Perform a variable assignment, like the [\set](../ysqlsh-meta-commands/#set-name-value) meta-command. Note that you must separate name and value, if any, by an equal sign (`=`) on the command line. To unset a variable, leave off the equal sign. To set a variable with an empty value, use the equal sign but leave off the value. These assignments are done during command line processing, so variables that reflect connection state are overwritten later.

##### -V, --version

Print the ysqlsh version and exit.

##### -w, --no-password

Never issue a password prompt. If the server requires password authentication and a password isn't available by other means such as a `~/.pgpass` file, the connection attempt fails. This option can be used in batch jobs and scripts where no user is present to enter a password.

Note that this option remains set for the entire session, and so it affects uses of the meta-command [\connect](../ysqlsh-meta-commands/#c-connect-reuse-previous-on-off-dbname-username-host-port-conninfo) as well as the initial connection attempt.

##### -W, --password

Force ysqlsh to prompt for a password before connecting to a database.

This option is never essential, as ysqlsh automatically prompts for a password if the server demands password authentication. However, ysqlsh wastes a connection attempt finding out that the server wants a password. In some cases it is worth typing `-W` to avoid the extra connection attempt.

Note that this option remains set for the entire session, and so it affects uses of the meta-command [\connect](../ysqlsh-meta-commands/#c-connect-reuse-previous-on-off-dbname-username-host-port-conninfo) as well as the initial connection attempt.

##### -x, --expanded

Turn on the expanded table formatting mode. This is equivalent to `\x` or [\pset expanded](../ysqlsh-pset-options/#expanded-or-x).

##### -X, --no-psqlrc

Don't read the start-up file (neither the system-wide `psqlrc` file nor the user's `~/.psqlrc` file).

##### -z, --field-separator-zero

Set the field separator for unaligned output to a zero byte. This is equivalent to [\pset fieldsep_zero](../ysqlsh-pset-options/#fieldsep-zero).

##### -0, --record-separator-zero

Set the record separator for unaligned output to a zero byte. This is helpful for interfacing, for example, with `xargs -0`. This is equivalent to [\pset recordsep_zero](../ysqlsh-pset-options/#recordsep-zero).

##### -1, --single-transaction

This option can only be used in combination with one or more `-c` and/or `-f` options. It causes ysqlsh to issue a [BEGIN](../../api/ysql/the-sql-language/statements/txn_begin/) statement before the first such option and a [COMMIT](../../api/ysql/the-sql-language/statements/txn_commit) statement after the last one, thereby wrapping all the commands into a single transaction. This ensures that either all the commands complete successfully, or no changes are applied.

If the statements themselves contain `BEGIN`, `COMMIT`, or [ROLLBACK](../../api/ysql/the-sql-language/statements/txn_rollback), this option won't have the desired effects. Also, if an individual statement cannot be executed inside a transaction block, specifying this option causes the whole transaction to fail.

##### -?, --help[=*topic*]

Show help about ysqlsh and exit. The optional *topic* parameter (defaulting to `options`) selects which part of ysqlsh is explained: `commands` describes ysqlsh's backslash commands; `options` describes the command-line options that can be passed to ysqlsh; and `variables` shows help about ysqlsh configuration variables.

## Variables

ysqlsh provides variable substitution features similar to common Linux command shells. Variables are name-value pairs, where the value can be any string of any length. The name must consist of letters (including non-Latin letters), digits, and underscores.

To set a variable, use the meta-command [\set](../ysqlsh-meta-commands/#set-name-value). For example:

```sql
testdb=> \set foo bar
```

sets the variable `foo` to the value `bar`. To retrieve the content of the variable, precede the name with a colon, for example:

```sql
testdb=> \echo :foo
bar
```

This works in both regular SQL statements and meta-commands; there is more detail in [SQL Interpolation](#sql-interpolation).

If you call `\set` without a second argument, the variable is set to an empty-string value. To unset (or delete) a variable, use the command [\unset](../ysqlsh-meta-commands/#unset-name). To show the values of all variables, call `\set` without any argument.

{{< note title="Note" >}}

The arguments of `\set` are subject to the same substitution rules as with other commands. Thus you can construct interesting references such as `\set :foo 'something'` and get "soft links" or "variable variables" of Perl or PHP fame, respectively. Unfortunately (or fortunately?), there is no way to do anything useful with these constructs. On the other hand, `\set bar :foo` is a perfectly valid way to copy a variable.

{{< /note >}}

A number of these variables are treated specially by ysqlsh. They represent certain option settings that can be changed at run time by altering the value of the variable, or in some cases represent changeable state of ysqlsh. By convention, all specially treated variables' names consist of all upper-case ASCII letters (and possibly digits and underscores). To ensure maximum compatibility in the future, avoid using such variable names for your own purposes.

Variables that control ysqlsh behavior generally can't be unset or set to invalid values. An `\unset` command is allowed but is interpreted as setting the variable to its default value. A `\set` command without a second argument is interpreted as setting the variable to `on`, for control variables that accept that value, and is rejected for others. Also, control variables that accept the values `on` and `off` also accept other common spellings of Boolean values, such as `true` and `false`.

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

If set to `all`, all nonempty input lines are printed to standard output as they are read. (This doesn't apply to lines read interactively.) To select this behavior on program startup, use the switch `-a`. If set to `queries`, ysqlsh prints each query to standard output as it is sent to the server. The switch to select this behavior is `-e`. If set to `errors`, then only failed queries are displayed on standard error output. The switch for this behavior is `-b`. If set to `none` (the default), then no queries are displayed.

##### ECHO_HIDDEN

When this variable is set to `on` and a backslash command queries the database, the query is first shown. This feature helps you to study YugabyteDB internals and provide similar functionality in your own programs. (To select this behavior on program start-up, use the switch `-E`.) If you set this variable to the value noexec, the queries are just shown but aren't actually sent to the server and executed. The default value is `off`.

##### ENCODING

The current client character set encoding. This is set every time you connect to a database (including program start-up), and when you change the encoding with [\encoding](../ysqlsh-meta-commands/#encoding-encoding), but it can be changed or unset.

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

in `~/.psqlrc` causes ysqlsh to maintain a separate history for each database.

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

If set to `1` or less, sending an EOF character (usually **Control+D**) to an interactive session of ysqlsh terminates the application. If set to a larger numeric value, that many consecutive EOF characters must be typed to make an interactive session terminate. If the variable is set to a non-numeric value, it is interpreted as `10`. The default is `0`.

{{< note title="Note" >}}

This feature was shamelessly plagiarized from Bash.

{{< /note >}}

##### LASTOID

The value of the last affected OID, as returned from an [`INSERT`](../../api/ysql/the-sql-language/statements/dml_insert) statement or [`\lo_import`](../ysqlsh-meta-commands/#lo-import-filename-comment) meta-command. This variable is only guaranteed to be valid until after the result of the next SQL statement has been displayed.

##### ON_ERROR_ROLLBACK

When set to `on`, if a statement in a transaction block generates an error, the error is ignored and the transaction continues. When set to `interactive`, such errors are only ignored in interactive sessions, and not when reading script files. When set to `off` (the default), a statement in a transaction block that generates an error aborts the entire transaction. The error rollback mode works by issuing an implicit `SAVEPOINT` for you, just before each statement that is in a transaction block, and then rolling back to the savepoint if the command fails.

##### ON_ERROR_STOP

By default, command processing continues after an error. When this variable is set to on, processing instead stops immediately. In interactive mode, ysqlsh returns to the command prompt; otherwise, ysqlsh exits, returning error code `3` to distinguish from fatal error conditions, which are reported using error code `1`. In either case, any currently running scripts (the top-level script, if any, and any other scripts which it may have in invoked) are terminated immediately. If the top-level command string contained multiple SQL statements, processing stops with the current statement.

##### PORT

The database server port to which you are currently connected. This is set every time you connect to a database (including program start-up), but can be changed or unset.

##### PROMPT1 / PROMPT2 / PROMPT3

These specify what the prompts ysqlsh issues should look like. See [Prompting](#prompting).

##### QUIET

Setting this variable to `on` is equivalent to the command line option `-q`. It is probably not too helpful in interactive mode.

##### SERVER_VERSION_NAME / SERVER_VERSION_NUM

The server's version number as a string, for example, `11.2-YB-2.0.7.0-b0`, and in numeric form, for example, `110002`. These are set every time you connect to a database (including program start-up), but can be changed or unset.

##### SHOW_CONTEXT

This variable can be set to the values `never`, `errors`, or `always` to control whether `CONTEXT` fields are displayed in messages from the server. The default is `errors` (meaning that context is shown in error messages, but not in notice or warning messages). This setting has no effect when [VERBOSITY](#verbosity) is set to `terse`. (See also [\errverbose](../ysqlsh-meta-commands/#errverbose), for use when you want a verbose version of the error you just got.)

##### SINGLELINE

Setting this variable to on is equivalent to the command line option `-S`.

##### SINGLESTEP

Setting this variable to on is equivalent to the command line option `-s`.

##### USER

The database user you are currently connected as. This is set every time you connect to a database (including program start-up), but can be changed or unset.

##### VERBOSITY

This variable can be set to the values `default`, `verbose`, or `terse` to control the verbosity of error reports. (See also [\errverbose](../ysqlsh-meta-commands/#errverbose), for use when you want a verbose version of the error you just got.)

##### VERSION / VERSION_NAME / VERSION_NUM

These variables are set at program start-up to reflect ysqlsh's version, respectively as a verbose string, a short string (for example, `9.6.2`, `10.1`, or `11beta1`), and a number (for example, `90602` or `100001`). They can be changed or unset.

### SQL interpolation

A key feature of ysqlsh variables is that you can substitute ("interpolate") them into regular SQL statements, as well as the arguments of meta-commands. Furthermore, ysqlsh provides facilities for ensuring that variable values used as SQL literals and identifiers are properly quoted. The syntax for interpolating a value without any quoting is to prepend the variable name with a colon (`:`). For example,

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

(Note that this still won't work if `my_file.txt` contains `NUL` bytes. ysqlsh doesn't support embedded `NUL` bytes in variable values.)

Because colons can legally appear in SQL statements, an apparent attempt at interpolation (that is, `:name`, `:'name'`, or `:"name"`) isn't replaced unless the named variable is currently set. In any case, you can escape a colon with a backslash to protect it from substitution.

The colon syntax for variables is standard SQL for embedded query languages, such as ECPG. The colon syntaxes for array slices and type casts are YugabyteDB extensions, which can sometimes conflict with the standard usage. The colon-quote syntax for escaping a variable's value as an SQL literal or identifier is a ysqlsh extension.

## Environment variables

Use the following environment variables to configure and customize your editor. Use the [\setenv](../ysqlsh-meta-commands/#setenv-name-value) meta-command to set environment variables. ysqlsh also uses the environment variables supported by `libpq`.

##### COLUMNS

If [\pset columns](../ysqlsh-pset-options/#columns) is zero (`0`), controls the width for the wrapped format and width for determining if wide output requires the pager or should be switched to the vertical format in expanded auto mode.

##### PAGER

If the query results don't fit on the screen, they are piped through this command. Typical values are `more` or `less`. The default is platform-dependent. Use of the pager can be disabled by setting `PAGER` to `empty`, or by using [pager-related options](#pager) of the `\pset` meta-command.

##### PGDATABASE, PGHOST, PGPORT, PGUSER

Default connection parameters.

##### PSQL_EDITOR, EDITOR, VISUAL

Editor used by the [\e](../ysqlsh-meta-commands/#e-edit-filename-line-number), [\ef](../ysqlsh-meta-commands/#ef-function-description-line-number), and [\ev](../ysqlsh-meta-commands/#ev-view-name-line-number) commands. These variables are examined in the order listed; the first that is set is used.

On Linux systems, the built-in default editor is `vi`.

##### PSQL_EDITOR_LINENUMBER_ARG

When [\e](../ysqlsh-meta-commands/#e-edit-filename-line-number), [\ef](../ysqlsh-meta-commands/#ef-function-description-line-number), and [\ev](../ysqlsh-meta-commands/#ev-view-name-line-number) is used with a line number argument, this variable specifies the command-line argument used to pass the starting line number to the user's editor. For editors such as `emacs` or `vi`, this is a plus (`+`) sign. Include a trailing space in the value of the variable if there needs to be space between the option name and the line number.

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

You can customize the prompts that ysqlsh issues. The variables [PROMPT1, PROMPT2, and PROMPT3](#prompt1-prompt2-prompt3) contain strings and special escape sequences that describe the appearance of the prompt.

Prompt 1 is the normal prompt that is issued when ysqlsh requests a new command.

Prompt 2 is issued when more input is expected during command entry, such as when the command wasn't terminated with a semicolon or a quote wasn't closed.

Prompt 3 is issued when you are running an SQL [COPY FROM STDIN](../../api/ysql/the-sql-language/statements/cmd_copy) statement and you need to type in a row value on the terminal.

ysqlsh prints the value of the selected prompt variable literally, except where it encounters a percent sign (`%`), in which case it substitutes other text as defined in [Prompt substitutions](#prompt-substitutions).

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

In prompt 1, normally `=`, but `@` if the session is in an inactive branch of a conditional block, or `^` if in single-line mode, or `!` if the session is disconnected from the database (which can happen if [\connect](../ysqlsh-meta-commands/#c-connect-reuse-previous-on-off-dbname-username-host-port-conninfo) fails).

In prompt 2, `%R` is replaced by a character that depends on why ysqlsh expects more input, as follows:

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

The value of the ysqlsh variable name. For details, see [Variables](#variables).

##### %\`*command*\`

The output of *command*, similar to ordinary "back-tick" substitution.

##### %[ ... %]

Prompts can contain terminal control characters. You can use these to, for example, change the color, background, or style of the prompt text, or change the title of the terminal window. For the line editing features of [Readline](#command-line-editing) to work properly, these non-printing control characters must be designated as invisible by enclosing them in `%[` and `%]`. Multiple pairs of these can occur in the prompt. For example:

```plpgsql
testdb=> \set PROMPT1 '%[%033[1;33;40m%]%n@%/%R%[%033[0m%]%# '
```

results in a boldfaced (`1;`) yellow-on-black (`33;40`) prompt on VT100-compatible, color-capable terminals.

### Command-line editing

ysqlsh supports the Readline library for convenient line editing and retrieval. The command history is automatically saved when ysqlsh exits and is reloaded when ysqlsh starts up. Tab-completion is also supported, although the completion logic makes no claim to be an SQL parser. The queries generated by tab-completion can also interfere with other SQL statements, for example, the [SET TRANSACTION ISOLATION LEVEL](../../api/ysql/the-sql-language/statements/txn_set) statement. If for some reason you don't like the tab completion, you can turn it off by putting this in a file named `.inputrc` in your home directory:

```sh
$if psql
set disable-completion on
$endif
```

(This is a feature of Readline, not ysqlsh. For details, see the Readline documentation.)

## Files

### psqlrc and ~/.psqlrc

Unless it is passed an `-X` option, ysqlsh attempts to read and execute commands from the system-wide startup file (`psqlrc`) and then the user's personal startup file (`~/.psqlrc`), after connecting to the database but before accepting normal commands. These files can be used to set up the client and the server to taste, typically with [\set](../ysqlsh-meta-commands/#set-name-value) and `SET` statements.

The system-wide startup file is named `psqlrc` and is sought in the installation's "system configuration" directory, which is most reliably identified by running `pg_config --sysconfdir`. By default this directory is `../etc/` relative to the directory containing the PostgreSQL executables. The name of this directory can be set explicitly using the `PGSYSCONFDIR` environment variable.

The user's personal startup file is named `.psqlrc` and is sought in the invoking user's home directory. The location of the user's startup file can be set explicitly through the [PSQLRC](#psqlrc) environment variable.

Both the system-wide startup file and the user's personal startup file can be made ysqlsh-version-specific by appending a dash and the YugabyteDB major or minor release number to the file name, for example `~/.psqlrc-10.2` or `~/.psqlrc-10.2.5`. The most specific version-matching file is read in preference to a non-version-specific file.

### .psql_history

The command-line history is stored in the file `~/.psql_history`.

The location of the history file can be set explicitly using the ysqlsh [HISTFILE](#histfile) variable or the [PSQL_HISTORY](#psql-history) environment variable.
