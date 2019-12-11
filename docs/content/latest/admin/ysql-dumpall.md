---
title: `ysql_dumpall`
linkTitle: `ysql_dumpall`
description: `ysql_dumpall`
headcontent: Extract all YugabyteDB databases and roles into a SQL script file.
menu:
  latest:
    identifier: `ysql_dumpall`
    parent: admin
    weight: 2420
isTocNested: true
showAsideToc: true
---

`ysql_dumpall` is a utility for writing out (“dumping”) all YugabyteDB databases of a cluster into one script file. The script file contains SQL commands that can be used as input to `psql` to restore the databases. It does this by calling ysql_dump for each database in the cluster. `ysql_dumpall` also dumps global objects that are common to all databases, database roles. (`ysql_dump` does not save roles.)

Since `ysql_dumpall` reads tables from all databases you will most likely have to connect as a database superuser in order to produce a complete dump. Also you will need superuser privileges to execute the saved script in order to be allowed to add roles and create databases.

The SQL script will be written to the standard output. Use the -f/--file option or shell operators to redirect it into a file.

`ysql_dumpall` needs to connect several times to the YugabyteDB server (once per database). If you use password authentication it will ask for a password each time. It is convenient to have a ~/.pgpass file in such cases. See Section 34.15 for more information.

## Syntax

```sh
ysql_dumpall [ <database-connection-option> ... ] [ <content-output-formation-option> ... ]
```

- *connection-option*: See [Database connection options](#database-connection-options).
- *content-output-format-option*: See [Content and output format options](#content-and-output-format-options)

## Database connection options

The following command-line options control the database connection parameters.

### --dbname

**Syntax**: `-dbname=<connection-string> | -d <connection-string>`

Specifies parameters used to connect to the server, as a connection string. See Section 34.1.1 for more information.

The option is called `--dbname` for consistency with other client applications, but because `ysql_dumpall` needs to connect to many databases, the database name in the connection string will be ignored. Use the `-l` option to specify the name of the database used for the initial connection, which will dump global objects and discover what other databases should be dumped.

### --host

**Syntax**: `--host=host | -h host`

Specifies the host name of the machine on which the database server is running. If the value begins with a slash, it is used as the directory for the Unix domain socket. The default is taken from the `PGHOST` environment variable, if set, else a Unix domain socket connection is attempted.

### --database

**Syntax**: `--database=dbname | -l dbname`

Specifies the name of the database to connect to for dumping global objects and discovering what other databases should be dumped. If not specified, the postgres database will be used, and if that does not exist, template1 will be used.

### --port

**Syntax**: `--port=port | -p port`

Specifies the TCP port or local Unix domain socket file extension on which the server is listening for connections. Defaults to the `PGPORT` environment variable, if set, or a compiled-in default.

### --username=username | -U username

The username to connect as.

### --no-password | -w

Never issue a password prompt. If the server requires password authentication and a password is not available by other means such as a `.pgpass` file, the connection attempt will fail. This option can be useful in batch jobs and scripts where no user is present to enter a password.

### --password | -W

Force `ysql_dumpall` to prompt for a password before connecting to a database.

This option is never essential, since `ysql_dumpall` will automatically prompt for a password if the server demands password authentication. However, `ysql_dumpall` will waste a connection attempt finding out that the server wants a password. In some cases it is worth typing `-W` to avoid the extra connection attempt.

Note that the password prompt will occur again for each database to be dumped. Usually, it's better to set up a `~/.pgpass` file than to rely on manual password entry.

### --role=rolename

Specifies a role name to be used to create the dump. This option causes `ysql_dumpall` to issue a `SET ROLE <rolename>` statement after connecting to the database. It is useful when the authenticated user (specified by `-U`) lacks privileges needed by `ysql_dumpall`, but can switch to a role with the required rights. Some installations have a policy against logging in directly as a superuser, and use of this option allows dumps to be made without violating the policy.

## Content and output formation options

The following command-line options control the content and format of the output.

### --data-only | -a

Dump only the data, not the schema (data definitions).

**Syntax**

```
ysql_dump ... --data-only | -a
```

### --clean | -c

Include SQL statements to clean (drop) databases before recreating them. `DROP` statements for roles are added as well.

### --encoding=encoding | -E encoding

Create the dump in the specified character set encoding. By default, the dump is created in the database encoding. (Another way to get the same result is to set the `PGCLIENTENCODING` environment variable to the desired dump encoding.)

### --file=filename | -f filename

Send output to the specified file. If this is omitted, the standard output is used.

### --globals-only | -g

Dump only global objects (roles), no databases.

### --oids | -o

Dump object identifiers (OIDs) as part of the data for every table. Use this option if your application references the OID columns in some way (that is, in a foreign key constraint). Otherwise, this option should not be used.

### --no-owner | -O

Do not output statements to set ownership of objects to match the original database. By default, `ysql_dumpall` issues `ALTER OWNER` or `SET SESSION AUTHORIZATION` statements to set ownership of created schema elements. These statements will fail when the script is run unless it is started by a superuser (or the same user that owns all of the objects in the script). To make a script that can be restored by any user, but will give that user ownership of all the objects, specify `-O`.

### --roles-only | -r

Dump only roles, no databases.

### --schema-only | -s

Dump only the object definitions (schema), not data.

### --superuser=username | -S username

Specify the superuser user name to use when disabling triggers. This is relevant only if `--disable-triggers` is used. (Usually, it's better to leave this out, and instead start the resulting script as superuser.)

### --verbose -V

Specifies verbose mode. This will cause `ysql_dumpall` to output start/stop times to the dump file, and progress messages to standard error. It will also enable verbose output `in ysql_dump`.

### --version | -V

Print the `ysql_dumpall` version and then exit.

### --no-privileges | --no-acl | -x

Prevent dumping of access privileges (`GRANT` and `REVOKE` statements).

### --attribute-inserts | --column-inserts

Dump data as `INSERT` statements with explicit column names (`INSERT INTO table (column, ...) VALUES ...`). This will make restoration very slow; it is mainly useful for making dumps that can be loaded into non-YugabyteDB databases.

### --disable-dollar-quoting

This option disables the use of dollar quoting for function bodies, and forces them to be quoted using SQL standard string syntax.

### --disable-triggers

This option is relevant only when creating a data-only dump. It instructs `ysql_dumpall` to include commands to temporarily disable triggers on the target tables while the data is reloaded. Use this if you have referential integrity checks or other triggers on the tables that you do not want to invoke during data reload.

Presently, the commands emitted for `--disable-triggers` must be done as superuser. So, you should also specify a superuser name with `-S`, or preferably be careful to start the resulting script as a superuser.

### --if-exists

Use conditional commands (that is, add an `IF EXISTS` clause) to drop databases and other objects. This option is not valid unless `--clean` is also specified.

### --inserts

Dump data as `INSERT` statements (rather than `COPY` statements). This will make restoration very slow; it is mainly useful for making dumps that can be loaded into non-YugabyteDB databases. Note that the restore might fail altogether if you have rearranged column order. The [`--column-inserts`](#column-inserts) option is safer, though even slower.

### --load-via-partition-root

When dumping data for a table partition, make the COPY or INSERT statements target the root of the partitioning hierarchy that contains it, rather than the partition itself. This causes the appropriate partition to be re-determined for each row when the data is loaded. This may be useful when reloading data on a server where rows do not always fall into the same partitions as they did on the original server. That could happen, for example, if the partitioning column is of type text and the two systems have different definitions of the collation used to sort the partitioning column.

### --lock-wait-timeout=timeout

Do not wait forever to acquire shared table locks at the beginning of the dump. Instead, fail if unable to lock a table within the specified timeout. The timeout may be specified in any of the formats accepted by `SET statement_timeout`. Allowed values vary depending on the server version you are dumping from, but an integer number of milliseconds is accepted by all versions.

### --no-comments

Do not dump comments.

### --no-publications

Do not dump publications.

### --no-role-passwords

Do not dump passwords for roles. When restored, roles will have a null password, and password authentication will always fail until the password is set. Since password values aren't needed when this option is specified, the role information is read from the catalog view `pg_roles` instead of `pg_authid`. Therefore, this option also helps if access to `pg_authid` is restricted by some security policy.

### --no-security-labels

Do not dump security labels.

### --no-subscriptions

Do not dump subscriptions.

### --no-sync

By default, `ysql_dumpall` will wait for all files to be written safely to disk. This option causes `ysql_dumpall` to return without waiting, which is faster, but means that a subsequent operating system crash can leave the dump corrupt. Generally, this option is useful for testing but should not be used when dumping data from production installation.

### --no-tablespaces

Do not output commands to create tablespaces nor select tablespaces for objects. With this option, all objects will be created in whichever tablespace is the default during restore.

### --no-unlogged-table-data

Do not dump the contents of unlogged tables. This option has no effect on whether or not the table definitions (schema) are dumped; it only suppresses dumping the table data.

### --quote-all-identifiers

Force quoting of all identifiers. This option is recommended when dumping a database from a server whose YugabyteDB major version is different from `ysql_dumpall`'s, or when the output is intended to be loaded into a server of a different major version. By default, `ysql_dumpall` quotes only identifiers that are reserved words in its own major version. This sometimes results in compatibility issues when dealing with servers of other versions that may have slightly different sets of reserved words. Using `--quote-all-identifiers` prevents such issues, at the price of a harder-to-read dump script.

### --use-set-session-authorization

Output SQL-standard `SET SESSION AUTHORIZATION` statements instead of `ALTER OWNER` statements to determine object ownership. This makes the dump more standards compatible, but depending on the history of the objects in the dump, might not restore properly.

### --help | -?

Show help about `ysql_dumpall` command line arguments and then exit.

## Environment

PGHOST PGPORT | PGOPTIONS | PGUSER

Default connection parameters

This utility, like most other YugabyteDB utilities, also uses the environment variables supported by `libpq`.

## Notes

Since `ysql_dumpall` calls `ysql_dump` internally, some diagnostic messages will refer to `ysql_dump`.

The `--clean` option can be useful even when your intention is to restore the dump script into a fresh cluster. Use of `--clean` authorizes the script to drop and recreate the built-in postgres and template1 databases, ensuring that those databases will retain the same properties (for instance, locale and encoding) that they had in the source cluster. Without the option, those databases will retain their existing database-level properties, as well as any pre-existing contents.

Once restored, it is wise to run `ANALYZE` on each database so the optimizer has useful statistics. You can also run `vacuumdb -a -z` to analyze all databases.

The dump script should not be expected to run completely without errors. In particular, because the script will issue `CREATE ROLE` statements for every role existing in the source cluster, it is certain to get a “role already exists” error for the bootstrap superuser, unless the destination cluster was initialized with a different bootstrap superuser name. This error is harmless and should be ignored. Use of the `--clean` option is likely to produce additional harmless error messages about non-existent objects, although you can minimize those by adding `--if-exists`.

`ysql_dumpall` requires all needed tablespace directories to exist before the restore; otherwise, database creation will fail for databases in non-default locations.

## Examples

### Dump all databases

```sh
$ ./postgres/bin/ysql_dumpall > db.out
```

To reload databases from this file, you can use:

```sh
$ ./bin/ysqlsh -f db.out yugabyte
```

It is not important to which database you connect here since the script file created by `ysql_dumpall` will contain the appropriate commands to create and connect to the saved databases. An exception is that if you specified `--clean`, you must connect to the postgres database initially; the script will attempt to drop other databases immediately, and that will fail for the database you are connected to.

## See Also

For details on possible error conditions, see [`ysql_dump`](./ysql_dump).
