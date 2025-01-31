---
title: REVOKE statement [YSQL]
headerTitle: REVOKE
linkTitle: REVOKE
description: Use the REVOKE statement to remove access privileges from one or more roles.
menu:
  preview_api:
    identifier: dcl_revoke
    parent: statements
aliases:
  - /preview/api/ysql/commands/dcl_revoke/
type: docs
---

## Synopsis

Use the `REVOKE` statement to remove access privileges from one or more roles.

## Syntax

{{%ebnf%}}
  revoke_table,
  revoke_table_col,
  revoke_seq,
  revoke_db,
  revoke_domain,
  revoke_schema,
  revoke_type,
  revoke_role
{{%/ebnf%}}

## Semantics

Any role has the sum of all privileges assigned to it. So, if `REVOKE` is used to revoke `SELECT` from `PUBLIC`, then it does not mean that all roles have lost `SELECT` privilege.
If a role had `SELECT` granted directly to it or inherited it via a group, then it can continue to hold the `SELECT` privilege.

If `GRANT OPTION FOR` is specified, only the grant option for the privilege is revoked, not the privilege itself. Otherwise, both the privilege and the grant option are revoked.

Similarly, while revoking a role, if `ADMIN OPTION FOR` is specified, then only the admin option for the privilege is revoked.

If a user holds a privilege with grant option and has granted it to other users, then revoking the privilege from the first user will also revoke it from dependent users
if `CASCADE` is specified. Otherwise, the `REVOKE` will fail.

When revoking privileges on a table, the corresponding column privileges (if any) are automatically revoked on each column of the table, as well. On the other hand, if a role has been granted privileges on a table, then revoking the same privileges from individual columns will have no effect.

## Examples

- Revoke SELECT privilege for PUBLIC on table 'stores'

```plpgsql
yugabyte=# REVOKE SELECT ON stores FROM PUBLIC;
```

- Remove user John from SysAdmins group.

```plpgsql
yugabyte=# REVOKE SysAdmins FROM John;
```

## See also

- [`CREATE ROLE`](../dcl_create_role)
- [`GRANT`](../dcl_grant)
