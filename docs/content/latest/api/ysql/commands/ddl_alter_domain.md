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

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <i class="fas fa-file-alt" aria-hidden="true"></i>
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <i class="fas fa-project-diagram" aria-hidden="true"></i>
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
    {{% includeMarkdown "../syntax_resources/commands/alter_domain_default,alter_domain_rename.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/alter_domain_default,alter_domain_rename.diagram.md" /%}}
  </div>
</div>


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

## See also

[`CREATE DOMAIN`](../ddl_create_domain)
[`DROP DOMAIN`](../ddl_drop_domain)
[Other YSQL Statements](..)
