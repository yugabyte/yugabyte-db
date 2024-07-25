---
title: ALTER PUBLICATION statement [YSQL]
headerTitle: ALTER PUBLICATION
linkTitle: ALTER PUBLICATION
description: Change properties of an existing publication.
menu:
  preview:
    identifier: ddl_alter_publication
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER PUBLICATION` statement to change properties of an existing publication.

## Syntax

{{%ebnf%}}
  alter_publication,
  alter_publication_action
{{%/ebnf%}}

## Semantics

### *alter_publication_action*

Specify one of the following actions.

#### ADD TABLE *table_name { ',' table_name }*

Add the specified tables to the publication.

#### SET TABLE *table_name { ',' table_name }*

Replace the list of tables/schemas in the publication with the specified list. The existing tables/schemas that were present in the publication will be removed.

#### DROP TABLE *table_name { ',' table_name }*

Remove the specified tables from the publication.

#### RENAME TO *publication_name*

Rename the publication name to the specified name.

#### OWNER TO *new_owner*

Change the owner of the publication to the *new_owner*.

### Permissions

To alter a publication, the invoking user must own the publication. Adding a table to a publication additionally requires owning that table. To alter the owner, you must also be a direct or indirect member of the new owning role. The new owner must have `CREATE` privilege on the database. Also, the new owner of a `FOR ALL TABLES` publication must be a superuser. However, a superuser can change the ownership of a publication regardless of these restrictions.

## Examples

Assume that you have created the tables `users`, `departments`, and `employees`.

To create a publication `my_publication`, use the following command:

```sql
yugabyte=# CREATE PUBLICATION my_publication FOR TABLE employees;
```

Add the `users` and `departments` tables to the publication `my_publication`:

```sql
yugabyte=# ALTER PUBLICATION my_publication ADD TABLE users, departments;
```

Drop tables `employees`from the publication `my_publication`.

```sql
yugabyte=# ALTER PUBLICATION my_publication DROP TABLE employees;
```

Set the tables to only `departments` in the publication `my_publication`. The table `users` will be removed from the publication as a result of the command.

```sql
yugabyte=# ALTER PUBLICATION my_publication SET TABLE departments;
```

Rename the publication `my_publication` to `renamed_publication`.

```sql
yugabyte=# ALTER PUBLICATION my_publication RENAME TO renamed_publication;
```

## See also

- [`CREATE PUBLICATION`](../ddl_create_publication)
- [`DROP PUBLICATION`](../ddl_drop_publication)
