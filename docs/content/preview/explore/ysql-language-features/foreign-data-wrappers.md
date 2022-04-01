---
title: Foreign Data Wrappers
linkTitle: Foreign Data Wrappers
description: Foreign Data Wrappers in YSQL
image: /images/section_icons/secure/create-roles.png
menu:
  latest:
    identifier: explore-ysql-language-features-foreign-data-wrappers
    parent: explore-ysql-language-features
    weight: 100
isTocNested: true
showAsideToc: true
---

This document describes how to use foreign data wrappers.

## Overview

A foreign data wrapper is a library that you can use to access and interact with an external data (foreign data) source. They allow you to query foreign objects from remote servers as if they were local objects.

In order to access foreign data, we first need to create the foreign data wrapper. Then, we need to create a foreign server, which specifies how to connect to the external data source. We may also need to create a user mapping which defines the mapping of a specific user to authorization credentials in the foreign server. Lastly, we need to create foreign tables, which represent the structure of the data on the external source. 

### CREATING FOREIGN DATA WRAPPERS

Use the [`CREATE FOREIGN DATA WRAPPER`](../../../api/ysql/the-sql-language/statements/ddl_create_foreign_data_wrapper/) command to create foreign data wrappers.

Example:

```plpgsql
yugabyte=# CREATE FOREIGN DATA WRAPPER mywrapper HANDLER myhandler OPTIONS (dummy 'true');
```

### CREATING A FOREIGN SERVER

You can use _foreign servers_ to specify connection information for an external data source.
Create foreign servers using the [`CREATE FOREIGN SERVER`](../../../api/ysql/the-sql-language/statements/ddl_create_server/) command.
Example:

```plpgsql
yugabyte=# CREATE SERVER myserver FOREIGN DATA WRAPPER mywrapper OPTIONS (host '197.0.2.1');
```

### CREATE USER MAPPINGS

User mappings associate a user with authorization credentials in the foreign server.
You can create a user mapping with the [`CREATE USER MAPPING`](../../../api/ysql/the-sql-language/statements/ddl_create_user_mapping.md) command.

List all databases using the following statements.

```plpgsql
yugabyte=# CREATE USER MAPPING FOR myuser SERVER myserver OPTIONS (user 'john', password 'password');
```

### CREATE FOREIGN TABLES

Use the [`CREATE FOREIGN TABLE`](../../api/ysql/the-sql-language/statements/ddl_create_foreign_table) command to create foreign tables.

```sql
yugabyte=# CREATE FOREIGN TABLE mytable (col1 int, col2 int)
           SERVER myserver
           OPTIONS (schema 'external_schema', table 'external_table');
```

The following foreign data wrappers are bundled with YugabyteDB:
- [postgres_fdw](../pg-extensions/#postgres-fdw-example)
- [file_fdw](../pg-extensions/#file-fdw-example)