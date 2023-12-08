---
title: Foreign data wrappers
linkTitle: Foreign data wrappers
description: Foreign data wrappers in YSQL
menu:
  stable:
    identifier: advanced-features-foreign-data-wrappers
    parent: advanced-features
    weight: 245
type: docs
---

A foreign data wrapper is a library that you can use to access and interact with an external data (foreign data) source. They allow you to query foreign objects from remote servers as if they were local objects.

To access foreign data, you first create a foreign data _wrapper_. Then, you create a foreign _server_, which specifies how to connect to the external data source. You may also need to create a user mapping to map a specific user to authorization credentials in the foreign server. Finally, you create foreign _tables_, which represent the structure of the data on the external source.

## Create a foreign data wrapper

Use the [`CREATE FOREIGN DATA WRAPPER`](../../../../api/ysql/the-sql-language/statements/ddl_create_foreign_data_wrapper/) command to create foreign data wrappers.

Example:

```plpgsql
yugabyte=# CREATE FOREIGN DATA WRAPPER mywrapper HANDLER myhandler OPTIONS (dummy 'true');
```

## Create a foreign server

You use _foreign servers_ to specify connection information for an external data source.
Create foreign servers using the [`CREATE FOREIGN SERVER`](../../../../api/ysql/the-sql-language/statements/ddl_create_server/) command.

Example:

```plpgsql
yugabyte=# CREATE SERVER myserver FOREIGN DATA WRAPPER mywrapper OPTIONS (host '197.0.2.1');
```

## Create user mappings

User mappings associate a user with authorization credentials in the foreign server.
You can create a user mapping with the [`CREATE USER MAPPING`](../../../../api/ysql/the-sql-language/statements/ddl_create_user_mapping) command.

Example:

```plpgsql
yugabyte=# CREATE USER MAPPING FOR myuser SERVER myserver OPTIONS (user 'john', password 'password');
```

## Create foreign tables

Use the [`CREATE FOREIGN TABLE`](../../../../api/ysql/the-sql-language/statements/ddl_create_foreign_table) command to create foreign tables.

```sql
yugabyte=# CREATE FOREIGN TABLE mytable (col1 int, col2 int)
           SERVER myserver
           OPTIONS (schema 'external_schema', table 'external_table');
```

The following foreign data wrappers are bundled with YugabyteDB:

- [postgres_fdw](../../pg-extensions/#postgres-fdw-example)
- [file_fdw](../../pg-extensions/#file-fdw-example)
