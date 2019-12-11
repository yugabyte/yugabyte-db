---
title: ysql_dump
linkTitle: ysql_dump
description: ysql_dump and ysql_dumpall
headcontent: Extract a YugabyteDB database into a SQL script file.
menu:
  latest:
    identifier: ysql_dump
    parent: admin
    weight: 2420
isTocNested: true
showAsideToc: true
---

`ysql_dump` is a utility for backing up a YugabyteDB database into a SQL script file. `ysql_dump` makes consistent backups, even if the database is being used concurrently. `ysql_dump` does not block other users accessing the database (readers or writers).

`ysql_dump` only dumps a single database. To backup global objects that are common to all databases in a cluster, such as roles, use `ysql_dumpall`.

Dumps can be output in SQL script files. Script dumps are plain-text files containing the SQL commands required to reconstruct the database to the state it was in at the time it was saved. To restore from such a script, import it using the `psql \i` command. Script files can be used to reconstruct the database even on other machines and other architectures; with some modifications, even on other SQL database products.

While running `ysql_dump`, you should examine the output for any warnings (printed on standard error), especially in light of the limitations listed below.

The `ysql_dump` utility is based on the PostgreSQL [`pg_dump`](https://www.postgresql.org/docs/10/app-pgdump.html) utility.

## Syntax

```sh
ysql_dump [ <connection-option>... ] [ <content-output-format-option> ... ] [ <dbname> ]
```

- *connection-option*: See [Database connection options](#database-connection-options).
- *content-output-format-option*: See [Content and output format options](#content-and-output-format-options)
- *dbname*: The name of the database.

## Database connection options

The following command-line options control the database connection parameters.

### -d | --dbname

Specifies the name of the database to connect to. This is equivalent to specifying `dbname` as the first non-option argument on the command line.

If this parameter contains an equal sign (`=`) or starts with a valid URI prefix (`yugabytedb://`), it is treated as a `conninfo` string.

### -h | --host

Specifies the host name of the machine on which the server is running. If the value begins with a slash (`/`), it is used as the directory for the Unix domain socket. Defaults to the compiled-in host of `127.0.0.1` else a Unix domain socket connection is attempted.

### -p | --port

Specifies the TCP port or local Unix domain socket file extension on which the server is listening for connections. Defaults to the compiled-in default port of `5433`.

### -U | --username

The username to connect as.

### -w | --no-password

Never issue a password prompt. If the server requires password authentication and a password is not available by other means such as a `.pgpass` file, the connection attempt will fail. This option can be useful in batch jobs and scripts where no user is present to enter a password.

### -W | --password

Force `ysql_dump` to prompt for a password before connecting to a database.

This option is never essential, since `ysql_dump` will automatically prompt for a password if the server demands password authentication. However, `ysql_dump` will waste a connection attempt finding out that the server wants a password. In some cases it is worth typing `-W` to avoid the extra connection attempt.

### --role

Specifies a role name to be used to create the dump. This option causes `ysql_dump` to issue a `SET ROLE <rolename>` statement after connecting to the database. It is useful when the authenticated user (specified by [`-U`](#u-username-username-username)) lacks privileges needed by `ysql_dump`, but can switch to a role with the required rights. Some installations have a policy against logging in directly as a superuser, and use of this option allows dumps to be made without violating the policy.

## Content and output format options

The following command-line options control the content and format of the output.

### dbname

Specifies the name of the database to be dumped. If this is not specified, the environment variable `PGDATABASE` is used. If that is not set, the user name specified for the connection is used.

### -a | --data-only

Dump only the data, not the schema (data definitions). Table data, large objects, and sequence values are dumped.

This option is similar to, but for historical reasons not identical to, specifying `--section=data`.

### -b | --blobs

Include large objects in the dump. This is the default behavior except when [`--schema`](#schema), [`--table`](#), or [`--schema-only`](#schema-only) is specified. The `-b` switch is therefore only useful to add large objects to dumps where a specific schema or table has been requested. Note that blobs are considered data and therefore will be included when [`--data-only`](#a-data-only) is used, but not when --schema-only is.

### -B | --no-blobs

Exclude large objects in the dump.

When both `-b` and `-B` are given, the behavior is to output large objects, when data is being dumped, see [`-b`](#b-blobs) option.

### -c | --clean

Output commands to clean (drop) database objects prior to outputting the commands for creating them. (Unless [`--if-exists`](#if-exists) is also specified, restore might generate some harmless error messages, if any objects were not present in the destination database.)

This option is only meaningful for the plain-text format.

### -C | --create

Begin the output with a command to create the database itself and reconnect to the created database. (With a script of this form, it doesn't matter which database in the destination installation you connect to before running the script.) If [`--clean`](#clean) is also specified, the script drops and recreates the target database before reconnecting to it.

This option is only meaningful for the plain-text format.

### -E | --encoding

Create the dump in the specified character set encoding. By default, the dump is created in the database encoding. (Another way to get the same result is to set the `PGCLIENTENCODING` environment variable to the desired dump encoding.)

### -f | --file

Send output to the specified file. This parameter can be omitted for file-based output formats, in which case the standard output is used.

### --schema | -n

Dump only schemas matching schema; this selects both the schema itself, and all its contained objects. When this option is not specified, all non-system schemas in the target database will be dumped. Multiple schemas can be selected by writing multiple `-n` switches. Also, the schema parameter is interpreted as a pattern according to the same rules used by the `psql \d` commands (see Patterns), so multiple schemas can also be selected by writing wildcard characters in the pattern. When using wildcards, be careful to quote the pattern if needed to prevent the shell from expanding the wildcards; see Examples.

{{< note title="Note" >}}

When `-n` is specified, `ysql_dump` makes no attempt to dump any other database objects that the selected schemas might depend upon. Therefore, there is no guarantee that the results of a specific-schema dump can be successfully restored by themselves into a clean database.

{{< /note >}}

Non-schema objects such as blobs are not dumped when `-n` is specified. You can add blobs back to the dump with the [`--blobs`](#b-blobs) switch.

{{< note title="Note" >}}

### --exclude-schema | -N

Do not dump any schemas matching the schema pattern. The pattern is interpreted according to the same rules as for [`--schema`](#schema-s) option. `-N` can be given more than once to exclude schemas matching any of several patterns.

When both `-n` and `-N` are given, the behavior is to dump just the schemas that match at least one `-n` switch but no `-N` switches. If `-N` appears without `-n`, then schemas matching `-N` are excluded from what is otherwise a normal dump.

### --oids | -o

Dump object identifiers (OIDs) as part of the data for every table. Use this option if your application references the OID columns in some way (for example, in a foreign key constraint). Otherwise, this option should not be used.

### --no-owner | -O

Do not output statements to set ownership of objects to match the original database. By default, `ysql_dump` issues `ALTER OWNER` or `SET SESSION AUTHORIZATION` statements to set ownership of created database objects. These statements will fail when the script is run unless it is started by a superuser (or the same user that owns all of the objects in the script). To make a script that can be restored by any user, but will give that user ownership of all the objects, specify `-O`.

### --schema-only | -s

Dump only the object definitions (schema), not data.

This option is the inverse of `--data-only`.

(Do not confuse this with the `--schema` option, which uses the word “schema” in a different meaning.)

To exclude table data for only a subset of tables in the database, see `--exclude-table-data`.

### --superuser | -S

Specify the superuser user name to use when disabling triggers. This is relevant only if [`--disable-triggers`](#disable-triggers) is used. (Usually, it's better to leave this out, and instead start the resulting script as superuser.)

### --table | -t

Dump only tables with names matching table. For this purpose, `table` includes views, materialized views, sequences, and foreign tables. Multiple tables can be selected by writing multiple `-t` switches. Also, the table parameter is interpreted as a pattern according to the same rules used by `psql \d` commands (see Patterns), so multiple tables can also be selected by writing wildcard characters in the pattern. When using wildcards, be careful to quote the pattern if needed to prevent the shell from expanding the wildcards; see [Examples](#examples).

The `-n` and `-N` switches have no effect when `-t` is used, because tables selected by `-t` will be dumped regardless of those switches, and non-table objects will not be dumped.

{{< note title="Note" >}}

When `-t` is specified, `ysql_dump` makes no attempt to dump any other database objects that the selected tables might depend upon. Therefore, there is no guarantee that the results of a specific-table dump can be successfully restored by themselves into a clean database.

{{< /note >}}

### --exclude-table | -T

Do not dump any tables matching the table pattern. The pattern is interpreted according to the same rules as for [`-t`](#t-table). `-T` can be given more than once to exclude tables matching any of several patterns.

When both `-t` and `-T` are given, the behavior is to dump just the tables that match at least one `-t` switch but no `-T` switches. If `-T` appears without `-t`, then tables matching `-T` are excluded from what is otherwise a normal dump.

### --verbose | -v

Specifies verbose mode. This will cause `ysql_dump` to output detailed object comments and start and stop times to the dump file, and progress messages to standard error.

### --version | -V

Print the `ysql_dump` version and exit.

### -x | --no-privileges | --no-acl

Prevent dumping of access privileges (GRANT and REVOKE statements).

### --compress | -Z

Specify the compression level to use. Zero (`0`) means no compression. For plain text output, setting a nonzero compression level causes the entire output file to be compressed, as though it had been fed through `gzip`; but the default is not to compress.

### --column-inserts | --attribute-inserts

Dump data as `INSERT` statements with explicit column names (`INSERT INTO table (column, ...) VALUES ...`). This will make restoration very slow; it is mainly useful for making dumps that can be loaded into non-YugabyteDB databases. However, since this option generates a separate command for each row, an error in reloading a row causes only that row to be lost rather than the entire table contents.

### --disable-dollar-quoting

This option disables the use of dollar quoting for function bodies, and forces them to be quoted using SQL standard string syntax.

### --disable-triggers

This option is relevant only when creating a data-only dump. It instructs `ysql_dump` to include commands to temporarily disable triggers on the target tables while the data is reloaded. Use this if you have referential integrity checks or other triggers on the tables that you do not want to invoke during data reload.

Presently, the commands emitted for `--disable-triggers` must be done as superuser. So, you should also specify a superuser name with [`-S`](#superuser-S), or preferably be careful to start the resulting script as a superuser.

### --enable-row-security

This option is relevant only when dumping the contents of a table which has row security. By default, `ysql_dump` will set `row_security` to `off`, to ensure that all data is dumped from the table. If the user does not have sufficient privileges to bypass row security, then an error is thrown. This parameter instructs `ysql_dump` to set `row_security` to `on` instead, allowing the user to dump the parts of the contents of the table that they have access to.

Note that if you use this option currently, you probably also want the dump be in `INSERT` format, as the `COPY FROM` during restore does not support row security.

### --exclude-table-data=*table*

Do not dump data for any tables matching the table pattern. The pattern is interpreted according to the same rules as for [`-t`](#t). The `--exclude-table-data` option can be given more than once to exclude tables matching any of several patterns. This option is useful when you need the definition of a particular table even though you do not need the data in it.

To exclude data for all tables in the database, see [`--schema-only`](#schema-only).

### --if-exists

Use conditional commands (that is, add an `IF EXISTS` clause) when cleaning database objects. This option is not valid unless [`--clean`](#clean) is also specified.

### --inserts

Dump data as `INSERT` statements (rather than `COPY` statements). This will make restoration very slow; it is mainly useful for making dumps that can be loaded into non-YugabyteDB databases. However, since this option generates a separate command for each row, an error in reloading a row causes only that row to be lost rather than the entire table contents. Note that the restore might fail altogether if you have rearranged column order. The [`--column-inserts`](#column-inserts) option is safe against column order changes, though even slower.

### --lock-wait-timeout=*timeout*

Do not wait forever to acquire shared table locks at the beginning of the dump. Instead fail if unable to lock a table within the specified timeout. The timeout may be specified in any of the formats accepted by `SET statement_timeout`. (Allowed formats vary depending on the server version you are dumping from, but an integer number of milliseconds is accepted by all versions.)

### --no-publications

Do not dump publications.

### --no-security-labels

Do not dump security labels.

### --no-subscriptions

Do not dump subscriptions.

### --no-sync

By default, `ysql_dump` will wait for all files to be written safely to disk. This option causes `ysql_dump` to return without waiting, which is faster, but means that a subsequent operating system crash can leave the dump corrupt. Generally, this option is useful for testing but should not be used when dumping data from production installation.

### --no-unlogged-table-data

Do not dump the contents of unlogged tables. This option has no effect on whether or not the table definitions (schema) are dumped; it only suppresses dumping the table data. Data in unlogged tables is always excluded when dumping from a standby server.

### --quote-all-identifiers

Force quoting of all identifiers. This option is recommended when dumping a database from a server whose YugabyteDB major version is different from `ysql_dump`, or when the output is intended to be loaded into a server of a different major version. By default, `ysql_dump` quotes only identifiers that are reserved words in its own major version. This sometimes results in compatibility issues when dealing with servers of other versions that may have slightly different sets of reserved words. Using [`--quote-all-identifiers`](#quote-all-identifiers) prevents such issues, at the price of a harder-to-read dump script.

### --section=*sectionname*

Only dump the named section. The section name can be pre-data, data, or post-data. This option can be specified more than once to select multiple sections. The default is to dump all sections.

The data section contains actual table data, large-object contents, and sequence values. Post-data items include definitions of indexes, triggers, rules, and constraints other than validated check constraints. Pre-data items include all other data definition items.

### --serializable-deferrable

Use a serializable transaction for the dump to ensure that the snapshot used is consistent with later database states by waiting for a point in the transaction stream at which no anomalies can be present, so that there is no risk of the dump failing or causing other transactions to roll back with a `serialization_failure`.

If there are active read-write transactions, the maximum wait time until the start of the dump will be `50ms` (based on the default [`--max_clock_skew_usec`](../../../admin/yb-tserver.md) for YB-TServer and YB-Master services.) If there are no active read-write transactions when `ysql_dump` is started, this option will not make any difference. Once running, performance with or without the switch is the same.

### --snapshot

Use the specified synchronized snapshot when making a dump of the database. This option is useful when needing to synchronize the dump with a logical replication slot or with a concurrent session. In the case of a parallel dump, the snapshot name defined by this option is used rather than taking a new snapshot.

### --strict-names

Require that each schema ([`--schema`](#schema-n)) and table ([`--table`](#table-t)) qualifier match at least one schema or table in the database to be dumped. Note that if none of the schema or table qualifiers find matches, `ysql_dump` will generate an error even without `--strict-names`.

This option has no effect on [`--exclude-schema`](#exclude-schema), `-T` | `--exclude-table`, or `--exclude-table-data`. An exclude pattern failing to match any objects is not considered an error.

### --use-set-session-authorization

Output SQL-standard `SET SESSION AUTHORIZATION` statements instead of `ALTER OWNER` statements to determine object ownership. This makes the dump more standards-compatible, but depending on the history of the objects in the dump, might not restore properly. Also, a dump using `SET SESSION AUTHORIZATION` statements will certainly require superuser privileges to restore correctly, whereas `ALTER OWNER` statements requires lesser privileges.

**Syntax**



### -? | --help

Show help about `ysql_dump` command line arguments and then exit.

## Examples

### Dump a database called `mydb` into a SQL script file

```sh
$ ysql_dump mydb > mydb.sql
```

### Dump a single table named `mytable`

```sh
$ ysql_dump -t mytable mydb > mytable_mydb.sql
```

### Dump all schemas whose names start with `east` or `west` and end in `gsm`, excluding any schema whose names contain the word `test`

```sh
$ ysql_dump -n 'east*gsm' -n 'west*gsm' -N '*test*' mydb > myschemas_mydb.sql
```

Here's the same example, using regular expression notation to consolidate the switches:

```sh
$ ysql_dump -n '(east|west)*gsm' -N '*test*' mydb > myschemas_mydb.sql
```

### Dump all database objects except for tables whose names begin with `ts_`

```sh
$ ysql_dump -T 'ts_*' mydb > objects_mydb.sql
```
