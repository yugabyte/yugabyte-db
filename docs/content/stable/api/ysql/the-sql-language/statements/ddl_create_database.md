---
title: CREATE DATABASE statement [YSQL]
headerTitle: CREATE DATABASE
linkTitle: CREATE DATABASE
description: Use the CREATE DATABASE statement to create a database that functions as a grouping mechanism for database objects, such as tables.
menu:
  stable:
    identifier: ddl_create_database
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE DATABASE` statement to create a database that functions as a grouping mechanism for database objects, such as [tables](../ddl_create_table).

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link active" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade" role="tabpanel" aria-labelledby="grammar-tab">
{{% includeMarkdown "../../syntax_resources/the-sql-language/statements/create_database,create_database_options.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade show active" role="tabpanel" aria-labelledby="diagram-tab">
{{% includeMarkdown "../../syntax_resources/the-sql-language/statements/create_database,create_database_options.diagram.md" %}}
  </div>
</div>

## Semantics

### *create_database*

### CREATE DATABASE *name*

Specify the name of the database to be created. An error is raised if a YSQL database of the given `name` already exists.

### *create_database_options*

### [ WITH ] OWNER *user_name*

Specify the role name of the user who will own the new database. When not specified, the database creator is the owner.

### TEMPLATE *template*

Specify the name of the template from which the new database is created.

### ENCODING *encoding*

Specify the character set encoding to use in the new database.

### LC_COLLATE *lc_collate*

Specify the collation order (`LC_COLLATE`).

### LC_CTYPE *lc_ctype*

Specify the character classification (`LC_CTYPE`).

### ALLOW_CONNECTIONS *allowconn*

Specify `false` to disallow connections to the database. Default is `true`, which allows connections to the database.

### CONNECTION_LIMIT *connlimit*

Specify how many concurrent connections can be made to this database. Default of `-1` allows unlimited concurrent connections.

### IS_TEMPLATE *istemplate*

`true` — This database can be cloned by any user with `CREATEDB` privileges.
Specify `false` to only superusers or the owner of the database can clone it.

### COLOCATION

Specify `true` if tables for this database should be colocated on a single tablet by default. See [Colocated tables](../../../../../architecture/docdb-sharding/colocated-tables/) for details on when colocated tables are beneficial.

Default is `false` and every table in the database will have its own set of tablets.

## Examples

### Create a colocated database

```plpgsql
yugabyte=# CREATE DATABASE company WITH COLOCATION = true;
```

In this example, tables in the database `company` will be colocated on a single tablet by default.

## See also

- [`ALTER DATABASE`](../ddl_alter_db)
- [`DROP DATABASE`](../ddl_drop_database)
