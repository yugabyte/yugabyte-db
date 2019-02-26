---
title: REVOKE
description: REVOKE
summary: REVOKE
menu:
  latest:
    identifier: api-ysql-commands-revoke
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/dcl_revoke
isTocNested: true
showAsideToc: true
---

## Synopsis 

Remove access privileges.

## Syntax

### Diagrams

#### revoke
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="632" height="100" viewbox="0 0 632 100"><path class="connector" d="M0 52h5m69 0h10m75 0h10m38 0h10m113 0h10m54 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h20m24 0h20q5 0 5 5v20q0 5-5 5m-5 0h50m77 0h22m-109 25q0 5 5 5h5m79 0h5q5 0 5-5m-104-25q5 0 5 5v33q0 5 5 5h89q5 0 5-5v-33q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="35" width="69" height="25" rx="7"/><text class="text" x="15" y="52">REVOKE</text><a xlink:href="../grammar_diagrams#privileges"><rect class="rule" x="84" y="35" width="75" height="25"/><text class="text" x="94" y="52">privileges</text></a><rect class="literal" x="169" y="35" width="38" height="25" rx="7"/><text class="text" x="179" y="52">ON</text><a xlink:href="../grammar_diagrams#privilege-target"><rect class="rule" x="217" y="35" width="113" height="25"/><text class="text" x="227" y="52">privilege_target</text></a><rect class="literal" x="340" y="35" width="54" height="25" rx="7"/><text class="text" x="350" y="52">FROM</text><rect class="literal" x="439" y="5" width="24" height="25" rx="7"/><text class="text" x="449" y="22">,</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="424" y="35" width="54" height="25"/><text class="text" x="434" y="52">name</text></a><rect class="literal" x="528" y="35" width="77" height="25" rx="7"/><text class="text" x="538" y="52">CASCADE</text><rect class="literal" x="528" y="65" width="79" height="25" rx="7"/><text class="text" x="538" y="82">RESTRICT</text></svg>

### Grammar

```
revoke ::= REVOKE privileges ON privilege_target FROM name [, ...] [ CASCADE | RESTRICT ] ;
```

## Examples

- Create a sample role.

```sql
postgres=# CREATE USER John;
```

- Grant John all permissions on the `postgres` database.

```sql
postgres=# GRANT ALL ON DATABASE postgres TO John;
```

- Remove John's permissions from the `postgres` database.

```sql
postgres=# REVOKE ALL ON DATABASE postgres FROM John;
```

## See Also

[Other YSQL Statements](..)
