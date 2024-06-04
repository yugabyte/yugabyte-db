---
title: ysqlsh meta-commands
headerTitle: ysqlsh meta-commands
linkTitle: Meta-commands
description: YSQL shell meta-commands.
headcontent: Run commands without querying the database
menu:
  stable:
    identifier: ysqlsh-meta-commands
    parent: ysqlsh
    weight: 10
type: docs
---

Similarly to psql, ysqlsh provides a number of meta-commands that make ysqlsh more suitable for administration or scripting. Meta-commands are often called slash or backslash commands. Anything you enter in ysqlsh that begins with an unquoted backslash is a meta-command that is processed by ysqlsh itself.

{{< note title="Cloud shell" >}}

For security reasons, the YugabyteDB Managed cloud shell only has access to a subset of meta-commands. With the exception of read-only access to the `/share` directory to load the sample datasets, commands that access the filesystem don't work in cloud shell.

{{< /note >}}

For examples of using meta-commands, see [ysqlsh meta-command examples](../ysqlsh-meta-examples/).

## Syntax

The format of a ysqlsh command is the backslash (`\`), followed immediately by a command verb, then any arguments. Arguments are separated from the command verb and each other by any number of whitespace characters.

To include whitespace in an argument you can quote it with single quotes (`' '`). To include a single quote in an argument, write two single quotes in single-quoted text (`' ... '' ...'`). Anything contained in single quotes is furthermore subject to C-like substitutions for `\n` (new line), `\t` (tab), `\b` (backspace), `\r` (carriage return), `\f` (form feed), `\digits` (octal), and `\xdigits` (hexadecimal). A backslash preceding any other character in single-quoted text quotes that single character, whatever it is.

If an unquoted colon (`:`) followed by a ysqlsh variable name appears in an argument, it is replaced by the variable's value, as described in [SQL interpolation](../ysqlsh/#sql-interpolation). The forms `:'variable_name'` and `:"variable_name"` described there work as well.

In an argument, text that is enclosed in backquotes is taken as a command line that is passed to the shell. The output of the command (with any trailing newline removed) replaces the backquoted text. In the text enclosed in backquotes, no special quoting or other processing occurs, except that appearances of :variable_name where variable_name is a ysqlsh variable name are replaced by the variable's value. Also, appearances of `:'variable_name'` are replaced by the variable's value suitably quoted to become a single shell command argument. (The latter form is almost always preferable, unless you are very sure of what is in the variable.) Because carriage return and line feed characters cannot be safely quoted on all platforms, the `:'variable_name'` form prints an error message and doesn't substitute the variable value when such characters appear in the value.

Some commands take an SQL identifier (such as a table name) as argument. These arguments follow the syntax rules of SQL: Unquoted letters are forced to lowercase, while double quotes (`"`) protect letters from case conversion and allow incorporation of whitespace into the identifier. In double quotes, paired double quotes reduce to a single double quote in the resulting name. For example, `FOO"BAR"BAZ` is interpreted as `fooBARbaz`, and `"A weird"" name"` becomes `A weird" name`.

Parsing for arguments stops at the end of the line, or when another unquoted backslash is found. An unquoted backslash is taken as the beginning of a new meta-command. The special sequence `\\` (two backslashes) marks the end of arguments and continues parsing SQL commands, if any. That way SQL statements and ysqlsh commands can be freely mixed on a line. But in any case, the arguments of a meta-command cannot continue beyond the end of the line.

Many of the meta-commands act on the current query buffer. This buffer holds whatever SQL statement text has been typed but not yet sent to the server for execution. This includes previous input lines as well as any text appearing before the meta-command on the same line.

## Command reference

The following meta-commands are available.

##### \a

If the current table output format is unaligned, it is switched to aligned. If it isn't unaligned, it is set to unaligned. This command is kept for backwards compatibility. See [\pset](#pset-option-value) for a more general solution.

##### \c, \connect [ -reuse-previous=on|off ] [ *dbname* [ *username* ] [ *host* ] [ *port* ] | *conninfo* ]

Establishes a new connection to a YugabyteDB server. The connection parameters to use can be specified either using a positional syntax, or using *conninfo* connection strings.

Where the command omits *dname*, *user*, *host*, or *port*, the new connection can reuse values from the previous connection. By default, values from the previous connection are reused except when processing a *conninfo* string. Passing a first argument of `-reuse-previous=on` or `-reuse-previous=off` overrides that default. When the command neither specifies nor reuses a particular parameter, the `libpq` default is used. Specifying any of *dbname*, *username*, *host*, or *port* as `-` is equivalent to omitting that parameter.

If the new connection is successful, the previous connection is closed. If the connection attempt failed (wrong user name, access denied, etc.), the previous connection is only kept if ysqlsh is in interactive mode. When executing a non-interactive script, processing immediately stops with an error. This distinction was chosen as a user convenience against typos on the one hand, and a safety mechanism that scripts aren't accidentally acting on the wrong database on the other hand.

Examples:

```plpgsql
=> \c mydb myuser host.dom 6432
=> \c service=foo
=> \c "host=localhost port=5432 dbname=mydb connect_timeout=10 sslmode=disable"
=> \c postgresql://tom@localhost/mydb?application_name=myapp
\C [ title ]
```

Sets the title of any tables being printed as the result of a query or unset any such title. This command is equivalent to [\pset title](../ysqlsh-pset-options/#title-or-c). (The name of this command derives from "caption", as it was previously only used to set the caption in an HTML table.)

##### \cd [ *directory* ]

Changes the current working directory to *directory*. Without argument, changes to the current user's home directory.

{{< tip title="Tip" >}}

To print your current working directory, use `\! pwd`.

{{< /tip >}}

##### \conninfo

Outputs information about the current database connection, including database, user, host, and port.

##### \copy { *table* [ ( *column_list* ) ] | ( *query* ) } { from | to } { '*filename*' | program '*command*' | stdin | stdout | pstdin | pstdout } [ \[ with \] ( *option* [, ...] ) ]

Performs a frontend (client) copy. This is an operation that runs an SQL `COPY` statement, but instead of the server reading or writing the specified file, ysqlsh reads or writes the file and routes the data between the server and the local file system. This means that file accessibility and privileges are those of the local user, not the server, and no SQL superuser privileges are required.

When program is specified, *command* is executed by ysqlsh and the data passed from or to *command* is routed between the server and the client. Again, the execution privileges are those of the local user, not the server, and no SQL superuser privileges are required.

For `\copy ... from stdin`, data rows are read from the same source that issued the command, continuing until `\.` is read or the stream reaches EOF. You can use this option to populate tables in-line in a SQL script file. For `\copy ... to stdout`, output is sent to the same place as ysqlsh command output, and the COPY count command status isn't printed (as it might be confused with a data row). To read or write ysqlsh standard input or output, regardless of the current command source or `\o` option, write from `pstdin` or to `pstdout`.

The syntax of this command is similar to that of the SQL `COPY` statement. All options other than the data source or destination are as specified for `COPY`. Because of this, special parsing rules apply to the `\copy` meta-command. Unlike most other meta-commands, the entire remainder of the line is always taken to be the arguments of `\copy`, and neither variable interpolation nor backquote expansion are performed in the arguments.

{{< note title="Note" >}}

Another way to obtain the same result as `\copy ... to` is to use the SQL [COPY ... TO STDOUT](../../api/ysql/the-sql-language/statements/cmd_copy) statement and terminate it with `\g *filename*` or `\g |*program*`. Unlike `\copy`, this method allows the command to span multiple lines; also, variable interpolation and backquote expansion can be used.

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

For each relation (table, view, materialized view, index, sequence, or foreign table) or composite type matching the *pattern*, show all columns, their types, and any special attributes such as `NOT NULL` or defaults. Associated indexes, constraints, rules, and triggers are also shown. For foreign tables, the associated foreign server is shown as well. ("Matching the pattern" is defined in [Patterns](#patterns).)

For some types of relation, `\d` shows additional information for each column: column values for sequences, indexed expressions for indexes, and foreign data wrapper options for foreign tables.

The command form `\d+` is identical, except that more information is displayed: any comments associated with the columns of the table are shown, as is the presence of OIDs in the table, the view definition if the relation is a view.

By default, only user-created objects are shown; supply a pattern or the `S` modifier to include system objects.

{{< note title="Note" >}}

If `\d` is used without a *pattern* argument, it is equivalent to `\dtvmsE`, which shows a list of all visible tables, views, materialized views, sequences, and foreign tables.

{{< /note >}}

##### \da[S] [ [pattern](#patterns) ]

Lists aggregate functions, together with their return type and the data types they operate on. If *pattern* is specified, only aggregates whose names match the pattern are shown. By default, only user-created objects are shown; supply a pattern or the `S` modifier to include system objects.

##### \dA[+] [ [pattern](#patterns) ]

Lists access methods. If *pattern* is specified, only access methods whose names match the pattern are shown. If `+` is appended to the command name, each access method is listed with its associated handler function and description.

##### \db[+] [ [pattern](#patterns) ]

Lists tablespaces. If *pattern* is specified, only tablespaces whose names match the pattern are listed. If `+` is appended to the command name, each tablespace is listed with its associated options, on-disk size, permissions, and description.

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

Note: `\deu+` might also display the user name and password of the remote user, so care should be taken not to disclose them.

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

<!--
##### \dgr[+] [ [pattern](#patterns) ]

Lists database tablegroups. If the form `\dgr+` is used, additional information is shown about each tablegroup.

##### \dgrt[+] [ [pattern](#patterns) ]

Lists tables and indexes in database tablegroups matching the *pattern*. If the form `\dgrt+` is used, additional information is shown about each tablegroup and table.
-->

##### \dl

Alias for [`\lo_list`](#lo-list), which shows a list of large objects.

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

The [GRANT](../../api/ysql/the-sql-language/statements/dcl_grant) and [REVOKE](../../api/ysql/the-sql-language/statements/dcl_revoke) statements are used to set access privileges. The meaning of the privilege display is explained under [GRANT](../../api/ysql/the-sql-language/statements/dcl_grant).

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

The new contents of the query buffer are then re-parsed according to the normal rules of ysqlsh, treating the whole buffer as a single line. Any complete queries are immediately executed; that is, if the query buffer contains or ends with a semicolon, everything up to that point is executed. Whatever remains waits in the query buffer; type semicolon (`;`) or [\g](#g-filename-g-command) to send it, or [\r](#r-reset) to cancel it by clearing the query buffer. Treating the buffer as a single line primarily affects meta-commands: whatever is in the buffer after a meta-command is taken as arguments to the meta-command, even if it spans multiple lines. (Thus, you cannot make scripts using meta-commands this way. Use [\i](#i-filename-include-filename) for that.)

If a line number is specified, ysqlsh positions the cursor on the specified line of the file or query buffer. Note that if a single all-digits argument is given, ysqlsh assumes it is a line number, not a file name.

{{< note title="Tip" >}}

See [Environment variables](../ysqlsh/#environment-variables) for information on configuring and customizing your editor.

{{< /note >}}

##### \echo text [ ... ]

Prints the arguments to the standard output, separated by one space and followed by a newline. This can be used to intersperse information in the output of scripts. For example:

```shell
=> \echo `date`
Mon Dec 16 21:40:57 CEST 2019
```

If the first argument is an unquoted `-n`, the trailing newline isn't written.

{{< tip title="Tip" >}}

If you use the `\o` command to redirect your query output, you might wish to use [\qecho](#qecho-text) instead of this command.

{{< /tip >}}

##### \ef [ *function_description* [ *line_number* ] ]

Fetches and edits the definition of the named function, in the form of a [CREATE OR REPLACE FUNCTION](../../api/ysql/the-sql-language/statements/ddl_create_function) statement. Editing is done in the same way as for [\edit](#e-edit-filename-line-number). After the editor exits, the updated command waits in the query buffer; type semicolon (`;`) or [\g](#g-filename-g-command) to send it, or [\r](#r-reset) to cancel.

The target function can be specified by name alone, or by name and arguments, for example, `foo(integer, text)`. The argument types must be given if there is more than one function of the same name.

If no function is specified, a blank `CREATE FUNCTION` template is presented for editing.

If a line number is specified, ysqlsh positions the cursor on the specified line of the function body. (Note that the function body typically doesn't begin on the first line of the file.)

Unlike most other meta-commands, the entire remainder of the line is always taken to be the arguments of `\ef`, and neither variable interpolation nor backquote expansion are performed in the arguments.

{{< note title="Tip" >}}

See [Environment variables](../ysqlsh/#environment-variables) for information on configuring and customizing your editor.

{{< /note >}}

##### \encoding [ *encoding* ]

Sets the client character set encoding. Without an argument, this command shows the current encoding.

##### \errverbose

Repeats the most recent server error message at maximum verbosity, as though [VERBOSITY](../ysqlsh/#verbosity) were set to `verbose` and [SHOW_CONTEXT](../ysqlsh/#show-context) were set to `always`.

##### \ev [ view_name [ line_number ] ]

Fetches and edits the definition of the named view, in the form of a `CREATE OR REPLACE VIEW` statement. Editing is done in the same way as for \edit. After the editor exits, the updated command waits in the query buffer; type semicolon (`;`) or [\g](#g-filename-g-command) to send it, or [\r](#r-reset) to cancel.

If no view is specified, a blank `CREATE VIEW` template is presented for editing.

If a line number is specified, ysqlsh positions the cursor on the specified line of the view definition.

Unlike most other meta-commands, the entire remainder of the line is always taken to be the arguments of `\ev`, and neither variable interpolation nor backquote expansion are performed in the arguments.

##### \f [ string ]

Sets the field separator for unaligned query output. The default is the vertical bar (`|`). It is equivalent to [\pset fieldsep](../ysqlsh-pset-options/#fieldsep).

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

The generated queries are executed in the order in which the rows are returned, and left-to-right in each row if there is more than one column. `NULL` fields are ignored. The generated queries are sent literally to the server for processing, so they cannot be ysqlsh meta-commands nor contain ysqlsh variable references. If any individual query fails, execution of the remaining queries continues unless `ON_ERROR_STOP` is set. Execution of each query is subject to `ECHO` processing. (Setting [`ECHO`](../ysqlsh/#echo) to `all` or `queries` is often advisable when using `\gexec`.) Query logging, single-step mode, timing, and other query execution features apply to each generated query as well.

If the current query buffer is empty, the most recently sent query is re-executed instead.

##### \gset [ prefix ]

Sends the current query buffer to the server and stores the query's output into ysqlsh variables (see [Variables](../ysqlsh/#variables)). The query to be executed must return exactly one row. Each column of the row is stored into a separate variable, named the same as the column. For example:

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

Gives syntax help on the specified SQL statement. If command isn't specified, then ysqlsh lists all the commands for which syntax help is available. If command is an asterisk (`*`), then syntax help on all SQL statements is shown.

Unlike most other meta-commands, the entire remainder of the line is always taken to be the arguments of `\help`, and neither variable interpolation nor backquote expansion are performed in the arguments.

{{< note title="Note" >}}

To simplify typing, commands that consists of several words don't have to be quoted. Thus, it is fine to type `\help alter table`.

{{< /note >}}

##### \H, \html

Turns on HTML query output format. If the HTML format is already on, it is switched back to the default aligned text format. This command is for compatibility and convenience. See [\pset](#pset-option-value) about setting other output options.

##### \i *filename*, \include *filename*

Reads input from the file *filename* and executes it as though it had been typed on the keyboard.

If filename is `-` (hyphen), then standard input is read until an `EOF` indication or `\q` meta-command. This can be used to intersperse interactive input with input from files. Note that [Readline](../ysqlsh/#command-line-editing) behavior is used only if it is active at the outermost level.

{{< note title="Note" >}}

If you want to see the lines on the screen as they are read, you must set the variable `ECHO` to `all`.

{{< /note >}}

##### \if expression, \elif expression, \else, \endif

This group of commands implements nestable conditional blocks. A conditional block must begin with an `\if` and end with an `\endif`. In between there may be any number of `\elif` clauses, which may optionally be followed by a single `\else` clause. Ordinary queries and other types of backslash commands may (and usually do) appear between the commands forming a conditional block.

The `\if` and `\elif` commands read their arguments and evaluate them as a Boolean expression. If the expression yields true then processing continues normally; otherwise, lines are skipped until a matching `\elif`, `\else`, or `\endif` is reached. Once an `\if` or `\elif` test has succeeded, the arguments of later `\elif` commands in the same block aren't evaluated but are treated as false. Lines following an `\else` are processed only if no earlier matching `\if` or `\elif` succeeded.

The expression argument of an `\if` or `\elif` command is subject to variable interpolation and backquote expansion, just like any other backslash command argument. After that it is evaluated like the value of an `on` or `off` option variable. So a valid value is any unambiguous case-insensitive match for one of: `true`, `false`, `1`, `0`, `on`, `off`, `yes`, `no`. For example, `t`, `T`, and `tR` are all considered to be `true`.

Expressions that don't properly evaluate to `true` or `false` generate a warning and are treated as `false`.

Lines being skipped are parsed normally to identify queries and backslash commands, but queries aren't sent to the server, and backslash commands other than conditionals (`\if`, `\elif`, `\else`, `\endif`) are ignored. Conditional commands are checked only for valid nesting. Variable references in skipped lines aren't expanded, and backquote expansion isn't performed either.

All the backslash commands of a given conditional block must appear in the same source file. If EOF is reached on the main input file or an `\include`-ed file before all local `\if`-blocks have been closed, then ysqlsh raises an error.

Here is an example that checks for the existence of two separate records in the database and stores the results in separate ysqlsh variables:

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

To intersperse text output in between query results, use `\qecho`.

##### \p, \print

Print the current query buffer to the standard output. If the current query buffer is empty, the most recently executed query is printed instead.

##### \password [* username* ]

Changes the password of the specified user (by default, the current user). This command prompts for the new password, encrypts it, and sends it to the server as an [ALTER ROLE](../../api/ysql/the-sql-language/statements/dcl_alter_role) statement. This ensures that the new password doesn't appear in clear text in the command history, the server log, or elsewhere.

##### \prompt [ *text* ] *name*

Prompts the user to supply text, which is assigned to the variable *name*. An optional prompt string, *text*, can be specified. (For multi-word prompts, enclose the text in single quotes.)

By default, `\prompt` uses the terminal for input and output. However, if the `-f` command line switch was used, `\prompt` uses standard input and standard output.

##### \pset [ *option* [ *value* ] ]

Sets options affecting the output of query result tables. *option* indicates which option is to be set. The semantics of *value* vary depending on the selected option. For some options, omitting value causes the option to be toggled or unset, as described under the particular option. If no such behavior is mentioned, then omitting value just results in the current setting being displayed.

`\pset` without any arguments displays the current status of all printing options.

The *options* are defined in [pset options](../ysqlsh-pset-options/).

For examples using `\pset`, see [ysqlsh meta-command examples](../ysqlsh-meta-examples/).

There are various shortcut commands for `\pset`. See `\a`, `\C`, `\f`, `\H`, `\t`, `\T`, and `\x`.

##### \q, \quit

Quits ysqlsh. In a script file, only execution of that script is terminated.

##### \qecho *text* [ ... ]

Identical to `\echo` except that the output is written to the query output channel, as set by `\o`.

##### \r, \reset

Resets (clears) the query buffer.

##### \s [ *filename* ]

Print ysqlsh command line history to *filename*. If filename is omitted, the history is written to the standard output (using the pager if appropriate). This command isn't available if ysqlsh was built without [Readline](../ysqlsh/#command-line-editing) support.

##### \set [ *name* [ *value* [ ... ] ] ]

Sets the ysqlsh variable *name* to *value*, or if more than one value is given, to the concatenation of all of them. If only one argument is given, the variable is set to an empty-string value. To unset a variable, use the `\unset` command.

`\set` without any arguments displays the names and values of all currently-set ysqlsh variables.

Valid variable names can contain letters, digits, and underscores. Variable names are case-sensitive. Certain variables are special, in that they control ysqlsh's behavior or are automatically set to reflect connection state.

These variables are documented in [Variables](../ysqlsh/#variables).

{{< note title="Note" >}}

This command is unrelated to the SQL `SET` statement.

{{< /note >}}

##### \setenv *name* [ *value* ]

Sets the [environment variable](../ysqlsh/#environment-variables) *name* to *value*, or if *value* isn't supplied, un-sets the environment variable. For example:

```sql
testdb=> \setenv PAGER less
testdb=> \setenv LESS -imx4F
```

##### \sf[+] function_description

Fetches and shows the definition of the named function, in the form of a [CREATE OR REPLACE FUNCTION](../../api/ysql/the-sql-language/statements/ddl_create_function) statement. The definition is printed to the current query output channel, as set by `\o`.

The target function can be specified by name alone, or by name and arguments, for example, `foo(integer, text)`. The argument types must be given if there is more than one function of the same name.

If `+` is appended to the command name, then the output lines are numbered, with the first line of the function body being line `1`.

Unlike most other meta-commands, the entire remainder of the line is always taken to be the arguments of `\sf`, and neither variable interpolation nor backquote expansion are performed in the arguments.

##### \sv[+] *view_name*

Fetches and shows the definition of the named view, in the form of a [CREATE OR REPLACE VIEW](../../api/ysql/the-sql-language/statements/ddl_create_view) statement. The definition is printed to the current query output channel, as set by `\o`.

If `+` is appended to the command name, then the output lines are numbered from `1`.

Unlike most other meta-commands, the entire remainder of the line is always taken to be the arguments of `\sv`, and neither variable interpolation nor backquote expansion are performed in the arguments.

##### \t

Toggles the display of output column name headings and row count footer. This command is equivalent to [\pset tuples_only](../ysqlsh-pset-options/#tuples-only-or-t) and is provided for convenience.

##### \T *table_options*

Specifies attributes to be placed in the `table` tag in HTML output format. This command is equivalent to [\pset tableattr <table_options>](../ysqlsh-pset-options/#tableattr-or-t).

##### \timing [ on | off ]

With a parameter, turns displaying of how long each SQL statement takes `on` or `off`. Without a parameter, toggles the display between `on` and `off`. The display is in milliseconds; intervals longer than 1 second are also shown in minutes:seconds format, with hours and days fields added if needed.

##### \unset *name*

Un-sets (deletes) the ysqlsh variable *name*.

Most variables that control ysqlsh's behavior cannot be unset; instead, an `\unset` command is interpreted as setting them to their default values. See [Variables](../ysqlsh/#variables).

##### \w | \write *filename* | |*command*

Writes the current query buffer to the file *filename* or pipes it to the shell command *command*. If the current query buffer is empty, the most recently executed query is written instead.

If the argument begins with `|`, then the entire remainder of the line is taken to be the command to execute, and neither variable interpolation nor backquote expansion are performed in it. The rest of the line is passed literally to the shell.

##### \watch [ *seconds* ]

Repeatedly execute the current query buffer (as [`\g`](#g-filename-g-command) does) until interrupted or the query fails. Wait the specified number of seconds (default `2`) between executions. Each query result is displayed with a header that includes the [\pset title string](../ysqlsh-pset-options/#title-or-c) (if any), the time as of query start, and the delay interval.

If the current query buffer is empty, the most recently sent query is re-executed instead.

##### \x [ on | off | auto ]

Sets or toggles expanded table formatting mode. As such, it is equivalent to [\pset expanded](../ysqlsh-pset-options/#expanded-or-x).

##### \z [ [pattern](#patterns) ]

Lists tables, views and sequences with their associated access privileges. If a *pattern* is specified, only tables, views, and sequences whose names match the pattern are listed.

This is an alias for [\dp](#dp-pattern-patterns) ("display privileges").

##### \\! [ *command* ]

With no argument, escapes to a sub-shell; ysqlsh resumes when the sub-shell exits. With an argument, executes the shell command *command*.

Unlike most other meta-commands, the entire remainder of the line is always taken to be the arguments of `\!`, and neither variable interpolation nor backquote expansion are performed in the arguments. The rest of the line is passed literally to the shell.

##### \\? [ *topic* ]

Shows help. The optional *topic* parameter (defaulting to `commands`) selects which part of ysqlsh is explained: `commands` describes ysqlsh backslash commands; `options` describes the command-line options that can be passed to ysqlsh; and `variables` shows help about ysqlsh configuration variables.

## Patterns

The various `\d` commands accept a *pattern* parameter to specify the object names to be displayed. In the simplest case, a pattern is just the exact name of the object. The characters in a pattern are normally folded to lower case, just as in SQL names; for example, `\dt FOO` displays the table named `foo`. As in SQL names, placing double quotes around a pattern stops folding to lower case. Should you need to include an actual double quote character in a pattern, write it as a pair of double quotes in a double-quote sequence; again this is in accord with the rules for SQL quoted identifiers. For example, `\dt "FOO""BAR"` displays the table named `FOO"BAR` (not `foo"bar`). Unlike the normal rules for SQL names, you can put double quotes around just part of a pattern; for instance `\dt FOO"FOO"BAR` displays the table named `fooFOObar`.

Whenever the *pattern* parameter is omitted completely, the `\d` commands display all objects that are visible in the current schema search path — this is equivalent to using `*` as the pattern. (An object is said to be visible if its containing schema is in the search path and no object of the same kind and name appears earlier in the search path. This is equivalent to the statement that the object can be referenced by name without explicit schema qualification.) To see all objects in the database regardless of visibility, use `*.*` as the pattern.

In a pattern, `*` matches any sequence of characters (including no characters) and `?` matches any single character. (This notation is comparable to Unix shell file name patterns.) For example, `\dt int*` displays tables whose names begin with `int`. But in double quotes, `*` and `?` lose these special meanings and are just matched literally.

A pattern that contains a dot (`.`) is interpreted as a schema name pattern followed by an object name pattern. For example, `\dt foo*.*bar*` displays all tables whose table name includes bar that are in schemas whose schema name starts with foo. When no dot appears, then the pattern matches only objects that are visible in the current schema search path. Again, a dot in double quotes loses its special meaning and is matched literally.

Advanced users can use regular-expression notations such as character classes, for example `[0-9]` to match any digit. All regular expression special characters work as specified, except for `.` which is taken as a separator as mentioned above, `*` which is translated to the regular-expression notation `.*`, `?` which is translated to `.`, and `$` which is matched literally. You can emulate these pattern characters at need by writing `?` for `.`, (`R+|`) for `R*`, or (`R|`) for `R?`. `$` isn't needed as a regular-expression character because the pattern must match the whole name, unlike the usual interpretation of regular expressions (in other words, `$` is automatically appended to your pattern). Write `*` at the beginning and/or end if you don't wish the pattern to be anchored. Note that in double quotes, all regular expression special characters lose their special meanings and are matched literally. Also, the regular expression special characters are matched literally in operator name patterns (that is, the argument of `\do`).
