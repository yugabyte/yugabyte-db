---
title: IMPORT FOREIGN SCHEMA statement [YSQL]
headerTitle: IMPORT FOREIGN SCHEMA
linkTitle: IMPORT FOREIGN SCHEMA
description: Use the IMPORT FOREIGN SCHEMA statement to import tables from a foreign schema.
menu:
  v2.18:
    identifier: ddl_import_foreign_schema
    parent: statements
type: docs
---

## Synopsis

Use the `IMPORT FOREIGN SCHEMA`  command to create foreign tables that represent tables in a foreign schema on a foreign server. The newly created foreign tables are owned by the user who issued the command.
By default, all the tables in the foreign schema are imported. However, the user can specify which tables to import using the `LIMIT TO` and `EXCEPT` clauses.
To use this command, the user must have `USAGE` privileges on the foreign server, and `CREATE` privileges on the target schema.

## Syntax

{{%ebnf%}}
  import_foreign_schema
{{%/ebnf%}}

## Semantics

Create foreign tables (in *local_schema*) that represent tables in a foreign schema named *remote_schema* located on a foreign server named *server_name*.

### LIMIT TO
The `LIMIT TO` clause can be optionally used to specify which tables to import. All other tables will be ignored.

### EXCEPT
The `EXCEPT` clause can be optionally used to specify which tables to *not* import. All other tables will be ignored.


## See also

- [`CREATE SERVER`](../ddl_create_server)
- [`CREATE FOREIGN TABLE`](../ddl_create_foreign_table)
- [`IMPORT FOREIGN SCHEMA`](../ddl_import_foreign_schema)
