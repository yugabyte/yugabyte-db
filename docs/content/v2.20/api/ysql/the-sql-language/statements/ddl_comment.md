---
title: COMMENT statement [YSQL]
headerTitle: COMMENT
linkTitle: COMMENT
description: Use the COMMENT statement to set, update, or remove a comment on a database object.
menu:
  v2.20:
    identifier: ddl_comment
    parent: statements
type: docs
---

## Synopsis

Use the `COMMENT` statement to set, update, or remove a comment on a database object.

## Syntax

{{%ebnf%}}
  comment_on
{{%/ebnf%}}

## Semantics

To remove a comment, set the value to `NULL`.

### *comment_on*

#### COMMENT ON

Add or change a comment about a database object. To remove a comment, set the value to `NULL`.

### *aggregate_signature*

## Examples

### Add a comment

```plpgsql
COMMENT ON DATABASE postgres IS 'Default database';
```

```plpgsql
COMMENT ON INDEX index_name IS 'Special index';
```

### Remove a comment

```plpgsql
COMMENT ON TABLE some_table IS NULL;
```
