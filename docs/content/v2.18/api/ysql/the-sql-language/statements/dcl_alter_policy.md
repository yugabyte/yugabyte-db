---
title: ALTER POLICY statement [YSQL]
headerTitle: ALTER POLICY
linkTitle: ALTER POLICY
description: Use the ALTER POLICY statement to change the definition of a row level security policy.
menu:
  v2.18:
    identifier: dcl_alter_policy
    parent: statements
type: docs
---

## Synopsis

Use  the `ALTER POLICY` statement to change the definition of a row level security policy. It can be used to
change the roles that the policy applies to and the `USING` and `CHECK` expressions of the policy.

## Syntax

{{%ebnf%}}
  alter_policy,
  alter_policy_rename
{{%/ebnf%}}

Where

- `name` is the name of the policy being updated.
- `table_name` is the name of the table on which the policy applies.
- `new_name` is the new name of the policy.
- `role_name` is the role(s) to which the policy applies. Use `PUBLIC` if the policy should be
  applied to all roles.
- `using_expression` is a SQL conditional expression. Only rows for which the condition returns to
  true will be visible in a `SELECT` and available for modification in an `UPDATE` or `DELETE`.
- `check_expression` is a SQL conditional expression that is used only for `INSERT` and `UPDATE`
  queries. Only rows for which the expression evaluates to true will be allowed in an `INSERT` or
  `UPDATE`. Note that unlike `using_expression`, this is evaluated against the proposed new contents
  of the row.

## Examples

- Rename a policy.

```plpgsql
yugabyte=# ALTER POLICY p1 ON table_foo RENAME TO p2;
```

- Apply policy to all roles.

```plpgsql
yugabyte=# ALTER POLICY p1 ON table_foo TO PUBLIC;
```

## See also

- [`CREATE POLICY`](../dcl_create_policy)
- [`DROP POLICY`](../dcl_drop_policy)
- [`ALTER TABLE`](../ddl_alter_table)
