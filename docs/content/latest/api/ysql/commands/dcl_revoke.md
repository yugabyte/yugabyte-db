---
title: REVOKE
description: REVOKE Command
summary: REVOKE
menu:
  latest:
    identifier: api-ysql-commands-revoke
    parent: api-ysql-commands-revoke
aliases:
  - /latest/api/ysql/commands/dcl_revoke
isTocNested: true
showAsideToc: true
---

## Synopsis 

Remove access privileges.

## Syntax

### Diagrams
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="638" height="97" viewbox="0 0 638 97"><path class="connector" d="M0 50h5m68 0h10m79 0h10m38 0h10m117 0h10m54 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h20m24 0h21q5 0 5 5v19q0 5-5 5m-5 0h50m77 0h20m-107 24q0 5 5 5h5m77 0h5q5 0 5-5m-102-24q5 0 5 5v32q0 5 5 5h87q5 0 5-5v-32q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="34" width="68" height="24" rx="7"/><text class="text" x="15" y="50">REVOKE</text><a xlink:href="../grammar_diagrams#privileges"><rect class="rule" x="83" y="34" width="79" height="24"/><text class="text" x="93" y="50">privileges</text></a><rect class="literal" x="172" y="34" width="38" height="24" rx="7"/><text class="text" x="182" y="50">ON</text><a xlink:href="../grammar_diagrams#privilege-target"><rect class="rule" x="220" y="34" width="117" height="24"/><text class="text" x="230" y="50">privilege_target</text></a><rect class="literal" x="347" y="34" width="54" height="24" rx="7"/><text class="text" x="357" y="50">FROM</text><rect class="literal" x="446" y="5" width="24" height="24" rx="7"/><text class="text" x="456" y="21">,</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="431" y="34" width="55" height="24"/><text class="text" x="441" y="50">name</text></a><rect class="literal" x="536" y="34" width="77" height="24" rx="7"/><text class="text" x="546" y="50">CASCADE</text><rect class="literal" x="536" y="63" width="77" height="24" rx="7"/><text class="text" x="546" y="79">RESTRICT</text></svg>

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
