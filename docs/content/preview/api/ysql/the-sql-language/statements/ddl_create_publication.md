---
title: CREATE PUBLICATION [YSQL]
headerTitle: CREATE PUBLICATION
linkTitle: CREATE PUBLICATION
description: Use the CREATE PUBLICATION statement to create a publication in a database.
menu:
  preview:
    identifier: ddl_create_publication
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE PUBLICATION` statement to create a publication in a database. A publication is a set of changes generated from a table or a group of tables, and might also be described as a change set or replication set. It defines the publication name and the tables included in it.

## Syntax

{{%ebnf%}}
  create_publication,
  publication_for_option
{{%/ebnf%}}

## Semantics

Create a publication with *publication_name*. If `publication_name` already exists in the specified database, an error will be raised.

### Tables

The `FOR TABLE` option specifies a list of tables to add to the publication while the `FOR ALL TABLES` option marks the publication as one that replicates changes for all tables in the database, including tables created in the future.

If `FOR TABLE` or `FOR ALL TABLES` are not specified, then the publication starts out with an empty set of tables. This is useful if tables are to be added later.

### Permissions

To create a publication, the invoking user must have the `CREATE` privilege for the current database. (Superusers bypass this check.)

To add a table to a publication, the invoking user must have ownership rights on the table. The `FOR ALL TABLES` clause requires the invoking user to be a **superuser**.

### Limitations

Currently to publish a subset of operations (create, update, delete, truncate) via a Publication is not supported.

## Examples

### Publication with two tables

```sql
yugabyte=# CREATE PUBLICATION mypublication FOR TABLE users, departments;
```

In this example, the publication `mypublication` will consist of two tables, `users` and `departments`.

```sql{.nocopy}
yugabyte=# select * from pg_publication;
    pubname    | pubowner | puballtables | pubinsert | pubupdate | pubdelete | pubtruncate
---------------+----------+--------------+-----------+-----------+-----------+-------------
 mypublication |    13250 | f            | t         | t         | t         | t
(1 row)
```

### Publication for all tables

```sql
yugabyte=# CREATE PUBLICATION puballtables FOR ALL TABLES;
```

In this example, the publication `puballtables` will include all the current as well as future tables.

```sql{.nocopy}
yugabyte=# select * from pg_publication;
    pubname    | pubowner | puballtables | pubinsert | pubupdate | pubdelete | pubtruncate
---------------+----------+--------------+-----------+-----------+-----------+-------------
 puballtables  |    13250 | t            | t         | t         | t         | t
(1 row)
```

## See also

- [`ALTER PUBLICATION`](../ddl_alter_publication)
- [`DROP PUBLICATION`](../ddl_drop_publication)
