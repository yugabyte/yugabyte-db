---
title: DROP POLICY statement [YSQL]
headerTitle: DROP POLICY
linkTitle: DROP POLICY
description: Use the DROP POLICY statement to remove the specified row level security policy from the table.
menu:
  v2.20:
    identifier: dcl_drop_policy
    parent: statements
type: docs
---

## Synopsis

Use the `DROP POLICY` statement to remove the specified row level security policy from the table. Note that if all
policies for a table are removed and the table still has `ENABLE ROW LEVEL SECURITY`, then a default
deny all policy will be applied for the table.

## Syntax

{{%ebnf%}}
  drop_policy
{{%/ebnf%}}

Where

- `name` is the name of the policy to be removed.
- `table_name` is the name of the table that the policy is on.
- `CASCADE` / `RESTRICT` don't have any effect because table policies don't have any dependent objects.

## Example

- Drop a policy.

```plpgsql
yugabyte=# DROP POLICY p1 ON table_foo;
```

## See also

- [`ALTER POLICY`](../dcl_alter_policy)
- [`CREATE POLICY`](../dcl_create_policy)
- [`ALTER TABLE`](../ddl_alter_table)
