---
title: DROP TRIGGER statement [YSQL]
headerTitle: DROP TRIGGER
linkTitle: DROP TRIGGER
description: Use the DROP TRIGGER statement to remove a trigger from the database.
menu:
  stable:
    identifier: ddl_drop_trigger
    parent: statements
type: docs
---

## Synopsis

Use the `DROP TRIGGER` statement to remove a trigger from the database.

## Syntax

{{%ebnf%}}
  drop_trigger
{{%/ebnf%}}

## Semantics

- `RESTRICT` is the default and it will throw an error if any objects depend on the trigger.
- `CASCADE` will drop all objects that (transitively) depend on the trigger.


## Examples

```plpgsql
DROP TRIGGER update_moddatetime ON posts;
```

## See also

- [`CREATE TRIGGER`](../ddl_create_trigger)
- [`INSERT`](../dml_insert)
- [`UPDATE`](../dml_update/)
- [`DELETE`](../dml_delete/)
