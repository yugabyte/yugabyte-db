---
title: CREATE DATABASE
summary: Create a new database
description: CREATE DATABASE
menu:
  latest:
    identifier: api-ysql-commands-create-db
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/ddl_create_database/
isTocNested: true
showAsideToc: true
---

## Synopsis
The `CREATE DATABASE` command creates a `database` that functions as a grouping mechanism for database objects such as [tables](../ddl_create_table).

## Syntax

### Diagrams

#### create_database
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="466" height="49" viewbox="0 0 466 49"><path class="connector" d="M0 21h5m67 0h10m84 0h10m55 0h30m180 0h20m-215 0q5 0 5 5v8q0 5 5 5h190q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="67" height="24" rx="7"/><text class="text" x="15" y="21">CREATE</text><rect class="literal" x="82" y="5" width="84" height="24" rx="7"/><text class="text" x="92" y="21">DATABASE</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="176" y="5" width="55" height="24"/><text class="text" x="186" y="21">name</text></a><a xlink:href="../grammar_diagrams#create-database-options"><rect class="rule" x="261" y="5" width="180" height="24"/><text class="text" x="271" y="21">create_database_options</text></a></svg>

#### create_database_options

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="394" height="576" viewbox="0 0 394 576"><path class="connector" d="M0 21h25m50 0h20m-85 0q5 0 5 5v8q0 5 5 5h60q5 0 5-5v-8q0-5 5-5m5 0h30m65 0h30m30 0h20m-65 0q5 0 5 5v8q0 5 5 5h40q5 0 5-5v-8q0-5 5-5m5 0h10m89 0h20m-279 0q5 0 5 5v23q0 5 5 5h254q5 0 5-5v-23q0-5 5-5m5 0h5m-394 64h25m82 0h30m30 0h20m-65 0q5 0 5 5v8q0 5 5 5h40q5 0 5-5v-8q0-5 5-5m5 0h10m76 0h20m-283 0q5 0 5 5v23q0 5 5 5h258q5 0 5-5v-23q0-5 5-5m5 0h5m-298 64h25m84 0h30m30 0h20m-65 0q5 0 5 5v8q0 5 5 5h40q5 0 5-5v-8q0-5 5-5m5 0h10m78 0h20m-287 0q5 0 5 5v23q0 5 5 5h262q5 0 5-5v-23q0-5 5-5m5 0h5m-302 64h25m92 0h30m30 0h20m-65 0q5 0 5 5v8q0 5 5 5h40q5 0 5-5v-8q0-5 5-5m5 0h10m78 0h20m-295 0q5 0 5 5v23q0 5 5 5h270q5 0 5-5v-23q0-5 5-5m5 0h5m-310 64h25m78 0h30m30 0h20m-65 0q5 0 5 5v8q0 5 5 5h40q5 0 5-5v-8q0-5 5-5m5 0h10m70 0h20m-273 0q5 0 5 5v23q0 5 5 5h248q5 0 5-5v-23q0-5 5-5m5 0h5m-288 64h25m97 0h30m30 0h20m-65 0q5 0 5 5v8q0 5 5 5h40q5 0 5-5v-8q0-5 5-5m5 0h10m131 0h20m-353 0q5 0 5 5v23q0 5 5 5h328q5 0 5-5v-23q0-5 5-5m5 0h5m-368 64h25m153 0h30m30 0h20m-65 0q5 0 5 5v8q0 5 5 5h40q5 0 5-5v-8q0-5 5-5m5 0h10m82 0h20m-360 0q5 0 5 5v23q0 5 5 5h335q5 0 5-5v-23q0-5 5-5m5 0h5m-375 64h25m99 0h10m49 0h30m30 0h20m-65 0q5 0 5 5v8q0 5 5 5h40q5 0 5-5v-8q0-5 5-5m5 0h10m76 0h20m-359 0q5 0 5 5v23q0 5 5 5h334q5 0 5-5v-23q0-5 5-5m5 0h5m-374 64h25m99 0h30m30 0h20m-65 0q5 0 5 5v8q0 5 5 5h40q5 0 5-5v-8q0-5 5-5m5 0h10m86 0h20m-310 0q5 0 5 5v23q0 5 5 5h285q5 0 5-5v-23q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="50" height="24" rx="7"/><text class="text" x="35" y="21">WITH</text><rect class="literal" x="125" y="5" width="65" height="24" rx="7"/><text class="text" x="135" y="21">OWNER</text><rect class="literal" x="220" y="5" width="30" height="24" rx="7"/><text class="text" x="230" y="21">=</text><a xlink:href="../grammar_diagrams#user-name"><rect class="rule" x="280" y="5" width="89" height="24"/><text class="text" x="290" y="21">user_name</text></a><rect class="literal" x="25" y="69" width="82" height="24" rx="7"/><text class="text" x="35" y="85">TEMPLATE</text><rect class="literal" x="137" y="69" width="30" height="24" rx="7"/><text class="text" x="147" y="85">=</text><a xlink:href="../grammar_diagrams#template"><rect class="rule" x="197" y="69" width="76" height="24"/><text class="text" x="207" y="85">template</text></a><rect class="literal" x="25" y="133" width="84" height="24" rx="7"/><text class="text" x="35" y="149">ENCODING</text><rect class="literal" x="139" y="133" width="30" height="24" rx="7"/><text class="text" x="149" y="149">=</text><a xlink:href="../grammar_diagrams#encoding"><rect class="rule" x="199" y="133" width="78" height="24"/><text class="text" x="209" y="149">encoding</text></a><rect class="literal" x="25" y="197" width="92" height="24" rx="7"/><text class="text" x="35" y="213">LC_COLLATE</text><rect class="literal" x="147" y="197" width="30" height="24" rx="7"/><text class="text" x="157" y="213">=</text><a xlink:href="../grammar_diagrams#lc-collate"><rect class="rule" x="207" y="197" width="78" height="24"/><text class="text" x="217" y="213">lc_collate</text></a><rect class="literal" x="25" y="261" width="78" height="24" rx="7"/><text class="text" x="35" y="277">LC_CTYPE</text><rect class="literal" x="133" y="261" width="30" height="24" rx="7"/><text class="text" x="143" y="277">=</text><a xlink:href="../grammar_diagrams#lc-ctype"><rect class="rule" x="193" y="261" width="70" height="24"/><text class="text" x="203" y="277">lc_ctype</text></a><rect class="literal" x="25" y="325" width="97" height="24" rx="7"/><text class="text" x="35" y="341">TABLESPACE</text><rect class="literal" x="152" y="325" width="30" height="24" rx="7"/><text class="text" x="162" y="341">=</text><a xlink:href="../grammar_diagrams#tablespace-name"><rect class="rule" x="212" y="325" width="131" height="24"/><text class="text" x="222" y="341">tablespace_name</text></a><rect class="literal" x="25" y="389" width="153" height="24" rx="7"/><text class="text" x="35" y="405">ALLOW_CONNECTIONS</text><rect class="literal" x="208" y="389" width="30" height="24" rx="7"/><text class="text" x="218" y="405">=</text><a xlink:href="../grammar_diagrams#allowconn"><rect class="rule" x="268" y="389" width="82" height="24"/><text class="text" x="278" y="405">allowconn</text></a><rect class="literal" x="25" y="453" width="99" height="24" rx="7"/><text class="text" x="35" y="469">CONNECTION</text><rect class="literal" x="134" y="453" width="49" height="24" rx="7"/><text class="text" x="144" y="469">LIMIT</text><rect class="literal" x="213" y="453" width="30" height="24" rx="7"/><text class="text" x="223" y="469">=</text><a xlink:href="../grammar_diagrams#connlimit"><rect class="rule" x="273" y="453" width="76" height="24"/><text class="text" x="283" y="469">connlimit</text></a><rect class="literal" x="25" y="517" width="99" height="24" rx="7"/><text class="text" x="35" y="533">IS_TEMPLATE</text><rect class="literal" x="154" y="517" width="30" height="24" rx="7"/><text class="text" x="164" y="533">=</text><a xlink:href="../grammar_diagrams#istemplate"><rect class="rule" x="214" y="517" width="86" height="24"/><text class="text" x="224" y="533">istemplate</text></a></svg>

### Grammar
```
create_database ::= CREATE DATABASE name [ create_database_options ]

create_database_options ::=
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

- `lc_collate` specifies the collation order (LC_COLLATE).

- `lc_ctype` specifies the character classification (LC_CTYPE).

- `tablespace_name` specifies the tablespace that is associated with the database to be created.

- `allowconn` is either `true` or `false`.

- `connlimit` specifies the number of concurrent connections can be made to this database. -1 means there is no limit.

- `istemplate` is either `true` or `false`.

## Semantics

- An error is raised if YSQL database of the given `name` already exists.

- Some options in DATABASE are under development.

## See Also
[`ALTER DATABASE`](../ddl_alter_db)
[Other YSQL Statements](..)
