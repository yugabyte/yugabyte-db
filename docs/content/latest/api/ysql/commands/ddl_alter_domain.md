---
title: ALTER DOMAIN
linkTitle: ALTER DOMAIN
summary: Alter a domain in a database
description: ALTER DOMAIN
menu:
  latest:
    identifier: api-ysql-commands-alter-domain
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/ddl_alter_domain
isTocNested: true
showAsideToc: true
---

## Synopsis
`ALTER DOMAIN` changes or redefines one or more attributes of a domain.

## Syntax

### Diagrams

#### alter_domain_default

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="615" height="95" viewbox="0 0 615 95"><path class="connector" d="M0 37h5m58 0h10m70 0h10m106 0h50m-5 0q-5 0-5-5v-17q0-5 5-5h271q5 0 5 5v17q0 5-5 5m-266 0h20m43 0h10m75 0h10m83 0h20m-256 0q5 0 5 5v20q0 5 5 5h5m53 0h10m75 0h88q5 0 5-5v-20q0-5 5-5m5 0h40m-336 0q5 0 5 5v38q0 5 5 5h311q5 0 5-5v-38q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="20" width="58" height="25" rx="7"/><text class="text" x="15" y="37">ALTER</text><rect class="literal" x="73" y="20" width="70" height="25" rx="7"/><text class="text" x="83" y="37">DOMAIN</text><a xlink:href="../../grammar_diagrams#domain-name"><rect class="rule" x="153" y="20" width="106" height="25"/><text class="text" x="163" y="37">domain_name</text></a><rect class="literal" x="329" y="20" width="43" height="25" rx="7"/><text class="text" x="339" y="37">SET</text><rect class="literal" x="382" y="20" width="75" height="25" rx="7"/><text class="text" x="392" y="37">DEFAULT</text><a xlink:href="../../grammar_diagrams#expression"><rect class="rule" x="467" y="20" width="83" height="25"/><text class="text" x="477" y="37">expression</text></a><rect class="literal" x="329" y="50" width="53" height="25" rx="7"/><text class="text" x="339" y="67">DROP</text><rect class="literal" x="392" y="50" width="75" height="25" rx="7"/><text class="text" x="402" y="67">DEFAULT</text></svg>

#### alter_domain_rename

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="488" height="35" viewbox="0 0 488 35"><path class="connector" d="M0 22h5m58 0h10m70 0h10m106 0h10m71 0h10m36 0h10m87 0h5"/><rect class="literal" x="5" y="5" width="58" height="25" rx="7"/><text class="text" x="15" y="22">ALTER</text><rect class="literal" x="73" y="5" width="70" height="25" rx="7"/><text class="text" x="83" y="22">DOMAIN</text><a xlink:href="../../grammar_diagrams#domain-name"><rect class="rule" x="153" y="5" width="106" height="25"/><text class="text" x="163" y="22">domain_name</text></a><rect class="literal" x="269" y="5" width="71" height="25" rx="7"/><text class="text" x="279" y="22">RENAME</text><rect class="literal" x="350" y="5" width="36" height="25" rx="7"/><text class="text" x="360" y="22">TO</text><rect class="literal" x="396" y="5" width="87" height="25" rx="7"/><text class="text" x="406" y="22">new_name</text></svg>

### Grammar
```
alter_domain_default := ALTER DOMAIN name
    { SET DEFAULT expression | DROP DEFAULT }

alter_domain_rename := ALTER DOMAIN name
    RENAME TO new_name
```

Where 

- `SET/DROP DEFAULT` sets or removes the default value for a domain.
- `RENAME` changes the name of the domain.
- Other `ALTER DOMAIN` options are not yet supported.

## Semantics

- An error is raised if DOMAIN `name` does not exist or DOMAIN `new_name` already exists.

## Examples

```sql
postgres=# CREATE DOMAIN idx DEFAULT 5 CHECK (VALUE > 0);
```

```sql
postgres=# ALTER DOMAIN idx DROP DEFAULT;
```

```sql
postgres=# ALTER DOMAIN idx RENAME TO idx_new;
```

```sql
postgres=# DROP DOMAIN idx_new;
```

## See Also
[`CREATE DOMAIN`](../ddl_create_domain)
[`DROP DOMAIN`](../ddl_drop_domain)
[Other YSQL Statements](..)
