---
title: ALTER DATABASE
linkTitle: ALTER DATABASE
summary: Alter database
description: ALTER DATABASE
menu:
  latest:
    identifier: api-ysql-commands-alter-db
    parent: api-ysql-commands-alter-db
aliases:
  - /latest/api/ysql/commands/ddl_alter_db
isTocNested: true
showAsideToc: true
---

## Synopsis
ALTER DATABASE redefines the attributes of the specified database.

## Syntax

### Diagrams

#### alter_database
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="714" height="359" viewbox="0 0 714 359"><path class="connector" d="M0 36h5m57 0h10m84 0h10m55 0h50m50 0h20m-85 0q5 0 5 5v8q0 5 5 5h60q5 0 5-5v-8q0-5 5-5m5 0h30m-5 0q-5 0-5-5v-16q0-5 5-5h171q5 0 5 5v16q0 5-5 5m-5 0h177m-468 39q0 5 5 5h5m71 0h10m36 0h10m86 0h230q5 0 5-5m-458 29q0 5 5 5h5m65 0h10m36 0h30m89 0h47m-146 24q0 5 5 5h5m116 0h5q5 0 5-5m-141-24q5 0 5 5v48q0 5 5 5h5m112 0h9q5 0 5-5v-48q0-5 5-5m5 0h166q5 0 5-5m-458 87q0 5 5 5h5m43 0h10m97 0h10m121 0h162q5 0 5-5m-458 29q0 5 5 5h5m43 0h10m175 0h30m36 0h20m-71 0q5 0 5 5v19q0 5 5 5h5m30 0h11q5 0 5-5v-19q0-5 5-5m5 0h30m53 0h41m-109 0q5 0 5 5v19q0 5 5 5h5m74 0h5q5 0 5-5v-19q0-5 5-5m5 0h5q5 0 5-5m-458 58q0 5 5 5h5m43 0h10m175 0h10m54 0h10m77 0h64q5 0 5-5m-458 29q0 5 5 5h5m59 0h10m175 0h199q5 0 5-5m-458 29q0 5 5 5h5m59 0h10m40 0h334q5 0 5-5m-463-300q5 0 5 5v308q0 5 5 5h448q5 0 5-5v-308q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="20" width="57" height="24" rx="7"/><text class="text" x="15" y="36">ALTER</text><rect class="literal" x="72" y="20" width="84" height="24" rx="7"/><text class="text" x="82" y="36">DATABASE</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="166" y="20" width="55" height="24"/><text class="text" x="176" y="36">name</text></a><rect class="literal" x="271" y="20" width="50" height="24" rx="7"/><text class="text" x="281" y="36">WITH</text><a xlink:href="../grammar_diagrams#alter-database-option"><rect class="rule" x="371" y="20" width="161" height="24"/><text class="text" x="381" y="36">alter_database_option</text></a><rect class="literal" x="251" y="64" width="71" height="24" rx="7"/><text class="text" x="261" y="80">RENAME</text><rect class="literal" x="332" y="64" width="36" height="24" rx="7"/><text class="text" x="342" y="80">TO</text><a xlink:href="../grammar_diagrams#new-name"><rect class="rule" x="378" y="64" width="86" height="24"/><text class="text" x="388" y="80">new_name</text></a><rect class="literal" x="251" y="93" width="65" height="24" rx="7"/><text class="text" x="261" y="109">OWNER</text><rect class="literal" x="326" y="93" width="36" height="24" rx="7"/><text class="text" x="336" y="109">TO</text><a xlink:href="../grammar_diagrams#new-owner"><rect class="rule" x="392" y="93" width="89" height="24"/><text class="text" x="402" y="109">new_owner</text></a><rect class="literal" x="392" y="122" width="116" height="24" rx="7"/><text class="text" x="402" y="138">CURRENT_USER</text><rect class="literal" x="392" y="151" width="112" height="24" rx="7"/><text class="text" x="402" y="167">SESSION_USER</text><rect class="literal" x="251" y="180" width="43" height="24" rx="7"/><text class="text" x="261" y="196">SET</text><rect class="literal" x="304" y="180" width="97" height="24" rx="7"/><text class="text" x="314" y="196">TABLESPACE</text><a xlink:href="../grammar_diagrams#new-tablespace"><rect class="rule" x="411" y="180" width="121" height="24"/><text class="text" x="421" y="196">new_tablespace</text></a><rect class="literal" x="251" y="209" width="43" height="24" rx="7"/><text class="text" x="261" y="225">SET</text><a xlink:href="../grammar_diagrams#configuration-parameter"><rect class="rule" x="304" y="209" width="175" height="24"/><text class="text" x="314" y="225">configuration_parameter</text></a><rect class="literal" x="509" y="209" width="36" height="24" rx="7"/><text class="text" x="519" y="225">TO</text><rect class="literal" x="509" y="238" width="30" height="24" rx="7"/><text class="text" x="519" y="254">=</text><a xlink:href="../grammar_diagrams#value"><rect class="rule" x="595" y="209" width="53" height="24"/><text class="text" x="605" y="225">value</text></a><rect class="literal" x="595" y="238" width="74" height="24" rx="7"/><text class="text" x="605" y="254">DEFAULT</text><rect class="literal" x="251" y="267" width="43" height="24" rx="7"/><text class="text" x="261" y="283">SET</text><a xlink:href="../grammar_diagrams#configuration-parameter"><rect class="rule" x="304" y="267" width="175" height="24"/><text class="text" x="314" y="283">configuration_parameter</text></a><rect class="literal" x="489" y="267" width="54" height="24" rx="7"/><text class="text" x="499" y="283">FROM</text><rect class="literal" x="553" y="267" width="77" height="24" rx="7"/><text class="text" x="563" y="283">CURRENT</text><rect class="literal" x="251" y="296" width="59" height="24" rx="7"/><text class="text" x="261" y="312">RESET</text><a xlink:href="../grammar_diagrams#configuration-parameter"><rect class="rule" x="320" y="296" width="175" height="24"/><text class="text" x="330" y="312">configuration_parameter</text></a><rect class="literal" x="251" y="325" width="59" height="24" rx="7"/><text class="text" x="261" y="341">RESET</text><rect class="literal" x="320" y="325" width="40" height="24" rx="7"/><text class="text" x="330" y="341">ALL</text></svg>

#### alter_database_option
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="295" height="92" viewbox="0 0 295 92"><path class="connector" d="M0 21h25m153 0h10m82 0h20m-275 24q0 5 5 5h5m99 0h10m49 0h10m76 0h6q5 0 5-5m-270-24q5 0 5 5v48q0 5 5 5h5m99 0h10m86 0h55q5 0 5-5v-48q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="153" height="24" rx="7"/><text class="text" x="35" y="21">ALLOW_CONNECTIONS</text><a xlink:href="../grammar_diagrams#allowconn"><rect class="rule" x="188" y="5" width="82" height="24"/><text class="text" x="198" y="21">allowconn</text></a><rect class="literal" x="25" y="34" width="99" height="24" rx="7"/><text class="text" x="35" y="50">CONNECTION</text><rect class="literal" x="134" y="34" width="49" height="24" rx="7"/><text class="text" x="144" y="50">LIMIT</text><a xlink:href="../grammar_diagrams#connlimit"><rect class="rule" x="193" y="34" width="76" height="24"/><text class="text" x="203" y="50">connlimit</text></a><rect class="literal" x="25" y="63" width="99" height="24" rx="7"/><text class="text" x="35" y="79">IS_TEMPLATE</text><a xlink:href="../grammar_diagrams#istemplate"><rect class="rule" x="134" y="63" width="86" height="24"/><text class="text" x="144" y="79">istemplate</text></a></svg>

### Grammar
```
alter_database ::=
  ALTER DATABASE name { [ [ WITH ] alter_database_option [ ... ] ] |
                        RENAME TO new_name |
                        OWNER TO { new_owner | CURRENT_USER | SESSION_USER } |
                        SET TABLESPACE new_tablespace |
                        SET configuration_parameter { TO | = } { value | DEFAULT } |
                        SET configuration_parameter FROM CURRENT |
                        RESET configuration_parameter |
                        RESET ALL }

alter_database_option ::=
  { ALLOW_CONNECTIONS allowconn | CONNECTION LIMIT connlimit | IS_TEMPLATE istemplate }
```

where

- `name` is an identifier that specifies the database to be altered.

- tablespace_name specifies the new tablespace that is associated with the database.

- allowconn is either `true` or `false`.

- connlimit specifies the number of concurrent connections can be made to this database. -1 means there is no limit.

- istemplate is either `true` or `false`.

## Semantics

- Some options in DATABASE are under development.

## See Also
[`CREATE DATABASE`](../ddl_create_database)
[Other PostgreSQL Statements](..)
