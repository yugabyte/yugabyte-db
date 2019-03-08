---
title: CREATE DATABASE
summary: Create a new database
description: CREATE DATABASE
menu:
  latest:
    identifier: api-ysql-commands-create-db
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/ddl_create_database
isTocNested: true
showAsideToc: true
---

## Synopsis
The `CREATE DATABASE` statement creates a `database` that functions as a grouping mechanism for database objects such as [tables](../ddl_create_table).

## Syntax

### Diagram

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="3310" height="79" viewbox="0 0 3310 79"><path class="connector" d="M0 21h5m67 0h10m84 0h10m55 0h50m50 0h20m-85 0q5 0 5 5v8q0 5 5 5h60q5 0 5-5v-8q0-5 5-5m5 0h30m65 0h30m30 0h20m-65 0q5 0 5 5v8q0 5 5 5h40q5 0 5-5v-8q0-5 5-5m5 0h10m89 0h20m-279 0q5 0 5 5v23q0 5 5 5h254q5 0 5-5v-23q0-5 5-5m5 0h30m82 0h30m30 0h20m-65 0q5 0 5 5v8q0 5 5 5h40q5 0 5-5v-8q0-5 5-5m5 0h10m76 0h20m-283 0q5 0 5 5v23q0 5 5 5h258q5 0 5-5v-23q0-5 5-5m5 0h30m84 0h30m30 0h20m-65 0q5 0 5 5v8q0 5 5 5h40q5 0 5-5v-8q0-5 5-5m5 0h10m78 0h20m-287 0q5 0 5 5v23q0 5 5 5h262q5 0 5-5v-23q0-5 5-5m5 0h30m92 0h30m30 0h20m-65 0q5 0 5 5v8q0 5 5 5h40q5 0 5-5v-8q0-5 5-5m5 0h10m78 0h20m-295 0q5 0 5 5v23q0 5 5 5h270q5 0 5-5v-23q0-5 5-5m5 0h30m78 0h30m30 0h20m-65 0q5 0 5 5v8q0 5 5 5h40q5 0 5-5v-8q0-5 5-5m5 0h10m70 0h20m-273 0q5 0 5 5v23q0 5 5 5h248q5 0 5-5v-23q0-5 5-5m5 0h30m97 0h30m30 0h20m-65 0q5 0 5 5v8q0 5 5 5h40q5 0 5-5v-8q0-5 5-5m5 0h10m131 0h20m-353 0q5 0 5 5v23q0 5 5 5h328q5 0 5-5v-23q0-5 5-5m5 0h30m153 0h30m30 0h20m-65 0q5 0 5 5v8q0 5 5 5h40q5 0 5-5v-8q0-5 5-5m5 0h10m82 0h20m-360 0q5 0 5 5v23q0 5 5 5h335q5 0 5-5v-23q0-5 5-5m5 0h30m99 0h10m49 0h30m30 0h20m-65 0q5 0 5 5v8q0 5 5 5h40q5 0 5-5v-8q0-5 5-5m5 0h10m76 0h20m-359 0q5 0 5 5v23q0 5 5 5h334q5 0 5-5v-23q0-5 5-5m5 0h30m99 0h30m30 0h20m-65 0q5 0 5 5v8q0 5 5 5h40q5 0 5-5v-8q0-5 5-5m5 0h10m86 0h20m-310 0q5 0 5 5v23q0 5 5 5h285q5 0 5-5v-23q0-5 5-5m5 0h20m-3059 0q5 0 5 5v38q0 5 5 5h3034q5 0 5-5v-38q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="67" height="24" rx="7"/><text class="text" x="15" y="21">CREATE</text><rect class="literal" x="82" y="5" width="84" height="24" rx="7"/><text class="text" x="92" y="21">DATABASE</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="176" y="5" width="55" height="24"/><text class="text" x="186" y="21">name</text></a><rect class="literal" x="281" y="5" width="50" height="24" rx="7"/><text class="text" x="291" y="21">WITH</text><rect class="literal" x="381" y="5" width="65" height="24" rx="7"/><text class="text" x="391" y="21">OWNER</text><rect class="literal" x="476" y="5" width="30" height="24" rx="7"/><text class="text" x="486" y="21">=</text><a xlink:href="../grammar_diagrams#user-name"><rect class="rule" x="536" y="5" width="89" height="24"/><text class="text" x="546" y="21">user_name</text></a><rect class="literal" x="675" y="5" width="82" height="24" rx="7"/><text class="text" x="685" y="21">TEMPLATE</text><rect class="literal" x="787" y="5" width="30" height="24" rx="7"/><text class="text" x="797" y="21">=</text><a xlink:href="../grammar_diagrams#template"><rect class="rule" x="847" y="5" width="76" height="24"/><text class="text" x="857" y="21">template</text></a><rect class="literal" x="973" y="5" width="84" height="24" rx="7"/><text class="text" x="983" y="21">ENCODING</text><rect class="literal" x="1087" y="5" width="30" height="24" rx="7"/><text class="text" x="1097" y="21">=</text><a xlink:href="../grammar_diagrams#encoding"><rect class="rule" x="1147" y="5" width="78" height="24"/><text class="text" x="1157" y="21">encoding</text></a><rect class="literal" x="1275" y="5" width="92" height="24" rx="7"/><text class="text" x="1285" y="21">LC_COLLATE</text><rect class="literal" x="1397" y="5" width="30" height="24" rx="7"/><text class="text" x="1407" y="21">=</text><a xlink:href="../grammar_diagrams#lc-collate"><rect class="rule" x="1457" y="5" width="78" height="24"/><text class="text" x="1467" y="21">lc_collate</text></a><rect class="literal" x="1585" y="5" width="78" height="24" rx="7"/><text class="text" x="1595" y="21">LC_CTYPE</text><rect class="literal" x="1693" y="5" width="30" height="24" rx="7"/><text class="text" x="1703" y="21">=</text><a xlink:href="../grammar_diagrams#lc-ctype"><rect class="rule" x="1753" y="5" width="70" height="24"/><text class="text" x="1763" y="21">lc_ctype</text></a><rect class="literal" x="1873" y="5" width="97" height="24" rx="7"/><text class="text" x="1883" y="21">TABLESPACE</text><rect class="literal" x="2000" y="5" width="30" height="24" rx="7"/><text class="text" x="2010" y="21">=</text><a xlink:href="../grammar_diagrams#tablespace-name"><rect class="rule" x="2060" y="5" width="131" height="24"/><text class="text" x="2070" y="21">tablespace_name</text></a><rect class="literal" x="2241" y="5" width="153" height="24" rx="7"/><text class="text" x="2251" y="21">ALLOW_CONNECTIONS</text><rect class="literal" x="2424" y="5" width="30" height="24" rx="7"/><text class="text" x="2434" y="21">=</text><a xlink:href="../grammar_diagrams#allowconn"><rect class="rule" x="2484" y="5" width="82" height="24"/><text class="text" x="2494" y="21">allowconn</text></a><rect class="literal" x="2616" y="5" width="99" height="24" rx="7"/><text class="text" x="2626" y="21">CONNECTION</text><rect class="literal" x="2725" y="5" width="49" height="24" rx="7"/><text class="text" x="2735" y="21">LIMIT</text><rect class="literal" x="2804" y="5" width="30" height="24" rx="7"/><text class="text" x="2814" y="21">=</text><a xlink:href="../grammar_diagrams#connlimit"><rect class="rule" x="2864" y="5" width="76" height="24"/><text class="text" x="2874" y="21">connlimit</text></a><rect class="literal" x="2990" y="5" width="99" height="24" rx="7"/><text class="text" x="3000" y="21">IS_TEMPLATE</text><rect class="literal" x="3119" y="5" width="30" height="24" rx="7"/><text class="text" x="3129" y="21">=</text><a xlink:href="../grammar_diagrams#istemplate"><rect class="rule" x="3179" y="5" width="86" height="24"/><text class="text" x="3189" y="21">istemplate</text></a></svg>

### Grammar
```
create_database ::=
  CREATE DATABASE name
  [ [ WITH ] [ OWNER [=] user_name ]
    [ TEMPLATE [=] template ]
    [ ENCODING [=] encoding ]
    [ LC_COLLATE [=] lc_collate ]
    [ LC_CTYPE [=] lc_ctype ]
    [ TABLESPACE [=] tablespace_name ]
    [ ALLOW_CONNECTIONS [=] allowconn ]
    [ CONNECTION LIMIT [=] connlimit ]
    [ IS_TEMPLATE [=] istemplate ] ]
```
Where

- `name` is an identifier that specifies the database to be created.

- `user_name` specifies the user who will own the new database. When not specified, the database creator is the owner.

- `template` specifies name of the template from which the new database is created.

- `encoding` specifies the character set encoding to use in the new database.

- lc_collate specifies the collation order (LC_COLLATE).

- lc_ctype specifies the character classification (LC_CTYPE).

- tablespace_name specifies the tablespace that is associated with the database to be created.

- allowconn is either `true` or `false`.

- connlimit specifies the number of concurrent connections can be made to this database. -1 means there is no limit.

- istemplate is either `true` or `false`.

## Semantics

- An error is raised if a database with the specified `name` already exists.

- Some options in DATABASE are under development.

## See Also
[`ALTER DATABASE`](../ddl_alter_db)
[Other PostgreSQL Statements](..)
