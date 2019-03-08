---
title: CREATE SCHEMA
linkTitle: CREATE SCHEMA
summary: Create schema
description: CREATE SCHEMA
menu:
  latest:
    identifier: api-ysql-commands-create-schema
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/cmd_create_schema
isTocNested: true
showAsideToc: true
---

## Synopsis
`CREATE SCHEMA` inserts a new schema definition to the current database. Schema name must be unique.

## Syntax

### Diagram 
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="700" height="64" viewbox="0 0 700 64"><path class="connector" d="M0 36h5m67 0h10m71 0h30m30 0h10m45 0h10m61 0h20m-191 0q5 0 5 5v8q0 5 5 5h166q5 0 5-5v-8q0-5 5-5m5 0h10m110 0h50m-5 0q-5 0-5-5v-16q0-5 5-5h136q5 0 5 5v16q0 5-5 5m-5 0h40m-201 0q5 0 5 5v8q0 5 5 5h176q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="20" width="67" height="24" rx="7"/><text class="text" x="15" y="36">CREATE</text><rect class="literal" x="82" y="20" width="71" height="24" rx="7"/><text class="text" x="92" y="36">SCHEMA</text><rect class="literal" x="183" y="20" width="30" height="24" rx="7"/><text class="text" x="193" y="36">IF</text><rect class="literal" x="223" y="20" width="45" height="24" rx="7"/><text class="text" x="233" y="36">NOT</text><rect class="literal" x="278" y="20" width="61" height="24" rx="7"/><text class="text" x="288" y="36">EXISTS</text><a xlink:href="../grammar_diagrams#schema-name"><rect class="rule" x="369" y="20" width="110" height="24"/><text class="text" x="379" y="36">schema_name</text></a><a xlink:href="../grammar_diagrams#schema-element"><rect class="rule" x="529" y="20" width="126" height="24"/><text class="text" x="539" y="36">schema_element</text></a></svg>

### Grammar
```
create_schema ::= CREATE SCHEMA [ IF NOT EXISTS ] schema_name [ schema_element [ ... ] ]
```

Where
- `schema_name` specifies the schema to be created.

- `schema_element` is a SQL statement that `CREATE` a database object to be created within the schema.

## Semantics

- `AUTHORIZATION` clause is not yet supported.
- Only `CREATE TABLE`, `CREATE VIEW`, `CREATE INDEX`, `CREATE SEQUENCE`, `CREATE TRIGGER`, and `GRANT` can be use to create objects within `CREATE SCHEMA` statement. Other database objects must be created in separate commands after the schema is created.

## See Also
[`CREATE TABLE`](../ddl_create_table)
[`CREATE VIEW`](../ddl_create_view)
[`CREATE INDEX`](../ddl_create_index)
[`CREATE SEQUENCE`](../ddl_create_seq)
[Other PostgreSQL Statements](..)
