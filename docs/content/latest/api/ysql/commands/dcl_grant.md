---
title: GRANT
description: GRANT Command (underdevelopment)
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

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="732" height="78" viewbox="0 0 732 78"><path class="connector" d="M0 50h5m61 0h10m79 0h10m38 0h10m117 0h10m36 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h20m24 0h21q5 0 5 5v19q0 5-5 5m-5 0h50m50 0h10m61 0h10m65 0h20m-231 0q5 0 5 5v8q0 5 5 5h206q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="34" width="61" height="24" rx="7"/><text class="text" x="15" y="50">GRANT</text><a xlink:href="../grammar_diagrams#privileges"><rect class="rule" x="76" y="34" width="79" height="24"/><text class="text" x="86" y="50">privileges</text></a><rect class="literal" x="165" y="34" width="38" height="24" rx="7"/><text class="text" x="175" y="50">ON</text><a xlink:href="../grammar_diagrams#privilege-target"><rect class="rule" x="213" y="34" width="117" height="24"/><text class="text" x="223" y="50">privilege_target</text></a><rect class="literal" x="340" y="34" width="36" height="24" rx="7"/><text class="text" x="350" y="50">TO</text><rect class="literal" x="421" y="5" width="24" height="24" rx="7"/><text class="text" x="431" y="21">,</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="406" y="34" width="55" height="24"/><text class="text" x="416" y="50">name</text></a><rect class="literal" x="511" y="34" width="50" height="24" rx="7"/><text class="text" x="521" y="50">WITH</text><rect class="literal" x="571" y="34" width="61" height="24" rx="7"/><text class="text" x="581" y="50">GRANT</text><rect class="literal" x="642" y="34" width="65" height="24" rx="7"/><text class="text" x="652" y="50">OPTION</text></svg>

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