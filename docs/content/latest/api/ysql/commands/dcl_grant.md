---
title: GRANT
description: GRANT Command
summary: GRANT Command
menu:
  latest:
    identifier: api-ysql-commands-grant
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/dcl_grant
isTocNested: true
showAsideToc: true
---

## Synopsis 

`GRANT` allows access privileges.

## Syntax

### Diagrams

#### grant

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="727" height="80" viewbox="0 0 727 80"><path class="connector" d="M0 52h5m61 0h10m75 0h10m38 0h10m113 0h10m36 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h20m24 0h20q5 0 5 5v20q0 5-5 5m-5 0h50m53 0h10m61 0h10m66 0h20m-235 0q5 0 5 5v8q0 5 5 5h210q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="35" width="61" height="25" rx="7"/><text class="text" x="15" y="52">GRANT</text><a xlink:href="../grammar_diagrams#privileges"><rect class="rule" x="76" y="35" width="75" height="25"/><text class="text" x="86" y="52">privileges</text></a><rect class="literal" x="161" y="35" width="38" height="25" rx="7"/><text class="text" x="171" y="52">ON</text><a xlink:href="../grammar_diagrams#privilege-target"><rect class="rule" x="209" y="35" width="113" height="25"/><text class="text" x="219" y="52">privilege_target</text></a><rect class="literal" x="332" y="35" width="36" height="25" rx="7"/><text class="text" x="342" y="52">TO</text><rect class="literal" x="413" y="5" width="24" height="25" rx="7"/><text class="text" x="423" y="22">,</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="398" y="35" width="54" height="25"/><text class="text" x="408" y="52">name</text></a><rect class="literal" x="502" y="35" width="53" height="25" rx="7"/><text class="text" x="512" y="52">WITH</text><rect class="literal" x="565" y="35" width="61" height="25" rx="7"/><text class="text" x="575" y="52">GRANT</text><rect class="literal" x="636" y="35" width="66" height="25" rx="7"/><text class="text" x="646" y="52">OPTION</text></svg>

### Grammar

```
grant ::= GRANT privileges ON privilege_target TO name [, ...] [ WITH GRANT OPTION ] ;
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