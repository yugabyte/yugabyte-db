---
title: DROP DATABASE
summary: Remove a database
description: DROP DATABASE
block_indexing: true
menu:
  v1.2:
    identifier: api-ysql-commands-drop-database
    parent: api-ysql-commands
isTocNested: true
showAsideToc: true
---

## Synopsis
The `DROP DATABASE` command removes a database and all of its associated objects from the system. This is an irreversible command. A currently-open connection to the database will be invalidated and then closed as soon as a command is executed via that connection.

## Syntax

### Diagrams
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="460" height="49" viewbox="0 0 460 49"><path class="connector" d="M0 21h15m54 0h10m84 0h30m30 0h10m61 0h20m-136 0q5 0 5 5v8q0 5 5 5h111q5 0 5-5v-8q0-5 5-5m5 0h10m121 0h15"/><polygon points="0,28 5,21 0,14" style="fill:black;stroke-width:0"/><rect class="literal" x="15" y="5" width="54" height="24" rx="7"/><text class="text" x="25" y="21">DROP</text><rect class="literal" x="79" y="5" width="84" height="24" rx="7"/><text class="text" x="89" y="21">DATABASE</text><rect class="literal" x="193" y="5" width="30" height="24" rx="7"/><text class="text" x="203" y="21">IF</text><rect class="literal" x="233" y="5" width="61" height="24" rx="7"/><text class="text" x="243" y="21">EXISTS</text><a xlink:href="../../grammar_diagrams#database-name"><rect class="rule" x="324" y="5" width="121" height="24"/><text class="text" x="334" y="21">database_name</text></a><polygon points="456,28 460,28 460,14 456,14" style="fill:black;stroke-width:0"/></svg>

### Grammar
```
drop_database ::= DROP DATABASE [ IF EXISTS ] database_name
```

Where

- `database_name` is a qualified name.

## Semantics

 - An error is raised if the specified `database_name` does not exist.
 - All objects that are associated with `database_name` such as tables will be invalidated after the drop statement is completed.
 - All connections to the dropped database would be invalidated and eventually disconnected.

## See Also

[`CREATE DATABASE`](../ddl_create_database)
[Other YSQL Statements](..)
