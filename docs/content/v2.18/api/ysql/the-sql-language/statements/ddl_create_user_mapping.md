---
title: CREATE USER MAPPING statement [YSQL]
headerTitle: CREATE USER MAPPING
linkTitle: CREATE USER MAPPING
description: Use the CREATE USER MAPPING statement to create a user mapping.
menu:
  v2.18:
    identifier: ddl_create_user_mapping
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE USER MAPPING` command to define the mapping of a specific user to authorization credentials in the foreign server. The foreign-data wrapper uses the information provided by the foreign server and the user mapping to connect to the external data source.

The owner of a foreign server can create user mappings for the server for any user. Moreover, a user can create user mapping for themself if they have `USAGE` privilege on the server.

## Syntax

{{%ebnf%}}
  create_user_mapping
{{%/ebnf%}}

## Semantics

Create a user mapping for the user *user_name* for the server *server_name*. If a mapping between the user and the foreign server already exists, an error will be raised unless the `IF NOT EXISTS` clause is used.

### Options:
The `OPTIONS` clause specifies options for the foreign-data server. They typically define the mapped username and password to be used on the external data source, but the actual permitted option names and values are specific to the server’s foreign data wrapper.


## Examples

Basic example.

```plpgsql
yugabyte=#  CREATE USER MAPPING FOR myuser SERVER my_server OPTIONS (user 'john', password 'password');
```

## See also

- [`CREATE FOREIGN DATA WRAPPER`](../ddl_create_foreign_data_wrapper/)
- [`CREATE FOREIGN TABLE`](../ddl_create_foreign_table/)
- [`CREATE SERVER`](../ddl_create_server/)
- [`IMPORT FOREIGN SCHEMA`](../ddl_import_foreign_schema/)
