---
title: Keyspaces and tables
linkTitle: Keyspaces and tables
description: Keyspaces and tables in YCQL
image: /images/section_icons/secure/create-roles.png
menu:
  preview:
    identifier: explore-ycql-language-features-keyspaces-tables
    parent: explore-ycql-language
    weight: 60
type: docs
---

This page explores keyspaces and tables in YCQL using the command line shell `ycqlsh`.
`ycqlsh`.

{{% explore-setup-single %}}

## YCQL shell

Use the [ycqlsh shell](../../../admin/ycqlsh/) to interact with a Yugabyte database cluster using the [YCQL API](../../../api/ycql/). Because `ycqlsh` is derived from the Apache Cassandra shell [cqlsh](https://cassandra.apache.org/doc/latest/cassandra/tools/cqlsh.html), most `cqlsh` commands work as is in `ycqlsh`. Unsupported commands are [listed](#unsupported-cqlsh-commands) at the end of this page.

Using `ycqlsh`, you can:

- interactively enter YCQL DDL and DML and see the query results
- input from a file or the command line
- use tab completion to automatically complete commands
- use a minimal command name as long as it can be distinguished from other commands (for example, `desc` instead of `DESCRIBE`)
  `desc` instead of `DESCRIBE`)

`ycqlsh` is installed with YugabyteDB and is located in the `bin` directory of the YugabyteDB home directory.

### Connect to a node

From your YugabyteDB home directory, connect to any node of the database cluster as follows:

```sh
$ ./bin/ycqlsh 127.0.0.1
```

This should bring up the following prompt, which prints the version of `ycqlsh` being used.

```output
Connected to local cluster at 127.0.0.1:9042.
[ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
ycqlsh>
```

## Users

By default, YugabyteDB has one admin user already created: `cassandra` (the recommended user), and you can check the existing user as follows:

```sql
ycqlsh> select * from system_auth.roles;
```

You should see an output similar to the following:

```output
 role      | can_login | is_superuser | member_of | salted_hash
-----------+-----------+--------------+-----------+------------------------------------------------------------------------------
 cassandra |      True |         True |          [] | $2a$12$64A8Vo0R3K9XeUp26CSzpuWtvUBwOiGFjPAbXGt7wsxZIScGrcsDu\x00\x00\x00\x00
```

## Keyspaces

YCQL supports keyspaces, much like Apache Cassandra, with one caveat. Keyspaces in Apache Cassandra
specify the replication properties for all tables in that keyspace. Replication configuration is defined at the [universe](../../../architecture/concepts/universe/) level in YugabyteDB.

To create a new keyspace `testdb`, run the following statement:

```sql
ycqlsh> CREATE KEYSPACE testdb;
```

To list all keyspaces, use the following command:

```sql
ycqlsh> DESCRIBE KEYSPACES;
```

```output
system_schema  system_auth  testdb  system
```

To use a specific keyspace, use the following command:

```sql
ycqlsh> USE testdb;
```

You should see the following output:

```output
ycqlsh:testdb>
```

To drop the keyspace you just created, use the `DROP` command as follows:

```sql
ycqlsh> DROP KEYSPACE testdb;
```

Verify the keyspace is no longer present as follows:

```sql
ycqlsh> DESCRIBE KEYSPACES;
```

```output
system_schema  system_auth  system
```

## Tables

Create a table using the CREATE TABLE statement.

```sql
CREATE TABLE users (
  id UUID,
  username TEXT,
  enabled boolean,
  PRIMARY KEY (id)
  );
```

To list all tables, use the following command:

```sql
ycqlsh> DESCRIBE TABLES;
```

```output
ycqlsh:testdb> DESCRIBE TABLES;

users
```

To describe the table you created, enter the following:

```sql
ycqlsh> DESCRIBE TABLE users;
```

```output
ycqlsh:testdb> describe table users;

CREATE TABLE testdb.users (
    id uuid PRIMARY KEY,
    username text,
    enabled boolean
) WITH default_time_to_live = 0
    AND transactions = {'enabled': 'false'};
```

{{< note title="Note" >}}
Due to architectural differences, YugabyteDB does _not_ support most of the Apache Cassandra table level properties.
`default_time_to_live` is one of the supported properties and the `transactions` property is added by YugabyteDB. For more details, see [table properties](../../../api/ycql/ddl_create_table/#table-properties-1).
{{< /note >}}
`default_time_to_live` is one of the supported properties. YugabyteDB adds the
`transactions` property. See [table properties](/preview/api/ycql/ddl_create_table/#table-properties-1) for more details.

## Quit ycqlsh

To quit the shell, enter the following command:

```sql
ycqlsh> quit;
```

## Unsupported `cqlsh` commands

|   Command         |                      Alternative                          |
| :---------------- | :-------------------------------------------------------- |
| LIST ROLES        | select * from system_auth.roles                           |
| SHOW SESSION      | Tracing from `ycqlsh` is not supported.                   |
| TRACING           | Tracing from `ycqlsh` is not supported.                   |
{.sno-1}
