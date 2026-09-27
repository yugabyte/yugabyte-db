---
title: LIST ROLES statement [YCQL]
headerTitle: LIST ROLES
linkTitle: LIST ROLES
description: Use the LIST ROLES statement to list roles and the roles granted to them.
menu:
  stable_api:
    parent: api-cassandra
    weight: 1285
type: docs
---

## Synopsis

Use the LIST ROLES statement to list roles, or the roles granted to a role.

This statement is enabled by setting the YB-TServer flag [--use_cassandra_authentication](../../../reference/configuration/yb-tserver/#ycql) to `true`.

## Syntax

```ebnf
list_roles := LIST ROLES [ OF role_name ] [ NORECURSIVE ] ;
```

Where

- `role_name` is a text identifier.

## Semantics

- Without `OF`, lists every role. With `OF role_name`, lists `role_name` itself and every role granted to it, directly or through other roles.
- With `NORECURSIVE`, only roles granted directly are listed (plus `role_name` itself). Without `OF`, `NORECURSIVE` applies to the roles of the current role, when those are what is listed (see [Permissions](#permissions)).
- The result has the columns `role` (text), `super` (boolean), `login` (boolean), and `options` (`map<text, text>`, always empty), ordered by role name.
- The role named in `OF` must exist; otherwise an error is returned.
- The result is always returned in a single page, regardless of the fetch size.

## Permissions

- A superuser, or a role that has `DESCRIBE` on `ALL ROLES` (directly or through a granted role), can list every role.
- Any other role can list only its own roles, meaning itself and the roles granted to it. For such a role, `LIST ROLES` without `OF` lists its own roles instead of failing, and `LIST ROLES OF role_name` fails with an unauthorized error unless `role_name` is one of its own roles.

## Differences from Apache Cassandra

- `LIST USERS` is not supported.

## Examples

```cql
cassandra@ycqlsh> CREATE ROLE engineering;
cassandra@ycqlsh> CREATE ROLE developer;
cassandra@ycqlsh> CREATE ROLE john WITH LOGIN = true AND PASSWORD = 'password';
cassandra@ycqlsh> GRANT engineering TO developer;
cassandra@ycqlsh> GRANT developer TO john;
```

### List all roles

```cql
cassandra@ycqlsh> LIST ROLES;
```

```output
 role        | super | login | options
-------------+-------+-------+---------
   cassandra |  True |  True |      {}
   developer | False | False |      {}
 engineering | False | False |      {}
        john | False |  True |      {}

(4 rows)
```

### List the roles granted to a role

```cql
cassandra@ycqlsh> LIST ROLES OF john;
```

```output
 role        | super | login | options
-------------+-------+-------+---------
   developer | False | False |      {}
 engineering | False | False |      {}
        john | False |  True |      {}

(3 rows)
```

### List only the roles granted directly

```cql
cassandra@ycqlsh> LIST ROLES OF john NORECURSIVE;
```

```output
 role      | super | login | options
-----------+-------+-------+---------
 developer | False | False |      {}
      john | False |  True |      {}

(2 rows)
```

## See also

- [CREATE ROLE](../ddl_create_role)
- [GRANT ROLE](../ddl_grant_role)
- [REVOKE ROLE](../ddl_revoke_role)
- [LIST PERMISSIONS](../ddl_list_permissions)
