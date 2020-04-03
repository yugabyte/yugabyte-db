---
title: CREATE INDEX
linkTitle: CREATE INDEX
summary: Create index on a table in a database
description: CREATE INDEX
block_indexing: true
menu:
  v2.0:
    identifier: api-ysql-commands-create-index
    parent: api-ysql-commands
isTocNested: true
showAsideToc: true
---

## Synopsis

This command creates an index on the specified column(s) of the specified table. Indexes are primarily used to improve query performance.

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
    {{% includeMarkdown "../syntax_resources/commands/create_index,index_elem.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/create_index,index_elem.diagram.md" /%}}
  </div>
</div>

## Semantics

`CONCURRENTLY`, `USING method`, `COLLATE`, and `TABLESPACE` options are not yet supported.

### UNIQUE

Enforce that duplicate values in a table are not allowed.

### INCLUDE clause

Specify a list of columns which will be included in the index as non-key columns.

#### *name*

 Specify the name of the index to be created.

#### *table_name*

Specify the name of the table to be indexed.

### *index_elem*

#### *column_name*

Specify the name of a column of the table.

#### *expression*

Specify one or more columns of the table and must be surrounded by parentheses.

- `HASH` - Use hash of the column. This is the default option for the first column and is used to hash partition the index table.
- `ASC` — Sort in ascending order. This is the default option for second and subsequent columns of the index.
- `DESC` — Sort in descending order.
- `NULLS FIRST` - Specifies that nulls sort before non-nulls. This is the default when DESC is specified.
- `NULLS LAST` - Specifies that nulls sort after non-nulls. This is the default when DESC is not specified.

## Examples

### Unique index with HASH column ordering

Create a unique index with hash ordered columns.

```postgresql
yugabyte=# CREATE TABLE products(id int PRIMARY KEY,
                                 name text,
                                 code text);
yugabyte=# CREATE UNIQUE INDEX ON products(code);
yugabyte=# \d products
              Table "public.products"
 Column |  Type   | Collation | Nullable | Default
--------+---------+-----------+----------+---------
 id     | integer |           | not null |
 name   | text    |           |          |
 code   | text    |           |          |
Indexes:
    "products_pkey" PRIMARY KEY, lsm (id HASH)
    "products_code_idx" UNIQUE, lsm (code HASH)
```

### ASC ordered index

Create an index with ascending ordered key.

```postgresql
yugabyte=# CREATE INDEX products_name ON products(name ASC);
yugabyte=# \d products_name
   Index "public.products_name"
 Column | Type | Key? | Definition
--------+------+------+------------
 name   | text | yes  | name
lsm, for table "public.products
```

### INCLUDE columns

Create an index with ascending ordered key and include other columns as non-key columns

```postgresql
yugabyte=# CREATE INDEX products_name_code ON products(name) INCLUDE (code);
yugabyte=# \d products_name_code;
 Index "public.products_name_code"
 Column | Type | Key? | Definition
--------+------+------+------------
 name   | text | yes  | name
 code   | text | no   | code
lsm, for table "public.products"
```
