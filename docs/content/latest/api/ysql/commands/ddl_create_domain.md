---
title: CREATE DOMAIN
linkTitle: CREATE DOMAIN
summary: Create a new domain in a database
description: CREATE DOMAIN
menu:
  latest:
    identifier: api-ysql-commands-create-domain
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/ddl_create_domain/
isTocNested: true
showAsideToc: true
---

## Synopsis

The `CREATE DOMAIN` command creates a user-defined data type with optional constraints such as range of valid values, DEFAULT, NOT NULL and CHECK. 
Domains are useful to abstract data types with common constraints. For example, domain can be used to represent phone number columns that will require the same CHECK constraints on the syntax.

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
    {{% includeMarkdown "../syntax_resources/commands/create_domain,domain_constraint.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/create_domain,domain_constraint.diagram.md" /%}}
  </div>
</div>

Where

- `name` is the name of the domain.
- `data_type` is the underlying data type.
- `DEFAULT expression` sets default value for columns of the domain data type.
- `CONSTRAINT constraint_name` is an optional name for constraint.
- `NOT NULL` does not allow null values.
- `NULL` allows null values (default).
- `CHECK (expression)` enforces a constraint that the values of the domain must satisfy and returns a boolean value.

The key word VALUE should be used to refer to the value being tested. Expressions evaluating to TRUE or UNKNOWN succeed.

## Semantics

- An error is raised if `name` already exists in the specified database.

## Examples

```sql
postgres=# CREATE DOMAIN phone_number AS TEXT CHECK(VALUE ~ '^\d{3}-\d{3}-\d{4}$');
```

```sql
postgres=# CREATE TABLE person(first_name TEXT, last_name TEXT, phone_number phone_number);
```

## See also

[`DROP DOMAIN`](../ddl_drop_domain)
[`ALTER DOMAIN`](../ddl_alter_domain)
[Other YSQL Statements](..)
