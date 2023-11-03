---
title: DROP DATABASE statement [YSQL]
headerTitle: DROP DATABASE
linkTitle: DROP DATABASE
description: Use the DROP DATABASE statement to remove a database and all of its associated objects from the system.
menu:
  preview:
    identifier: ddl_drop_database
    parent: statements
aliases:
  - /preview/api/ysql/commands/ddl_drop_database/
type: docs
---

## Synopsis

Use the `DROP DATABASE` statement to remove a database and all of its associated objects from the system. This is an irreversible statement. A currently-open connection to the database will be invalidated and then closed as soon as the statement is executed using that connection.

## Syntax

{{%ebnf%}}
  drop_database
{{%/ebnf%}}

## Semantics

### *drop_database*

#### DROP DATABASE [ IF EXISTS ] *database_name*

Remove a database and all associated objects. All objects that are associated with `database_name` such as tables will be invalidated after the drop statement is completed. All connections to the dropped database would be invalidated and eventually disconnected.

### *database_name*

Specify the name of the database.

## See also

- [`CREATE DATABASE`](../ddl_create_database)
