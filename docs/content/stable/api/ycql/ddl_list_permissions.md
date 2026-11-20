---
title: LIST PERMISSIONS statement [YCQL]
headerTitle: LIST PERMISSIONS
linkTitle: LIST PERMISSIONS
description: Use the LIST PERMISSIONS statement to list the permissions granted to roles.
menu:
  stable_api:
    parent: api-cassandra
    weight: 1286
type: docs
---

## Synopsis

Use the LIST PERMISSIONS statement to list the permissions granted to roles, optionally limited to one role, one resource, or one permission.

This statement is enabled by setting the YB-TServer flag [--use_cassandra_authentication](../../../reference/configuration/yb-tserver/#ycql) to `true`.

## Syntax

```ebnf
list_permissions := LIST ( all_permissions | permission ) [ ON resource ] [ OF role_name ]
                    [ NORECURSIVE ] ;
all_permissions := ALL [ PERMISSIONS ] ;
permission := ( CREATE | ALTER | DROP | SELECT | MODIFY | AUTHORIZE | DESCRIBE ) [ PERMISSION ] ;
resource := ALL ( KEYSPACES | ROLES ) | KEYSPACE keyspace_name | [ TABLE ] table_name
            | ROLE role_name ;
```

Where

- `keyspace_name`, `table_name`, and `role_name` are text identifiers (`table_name` may be qualified with a keyspace name).
- `permission` and `resource` are the same as in [GRANT PERMISSION](../ddl_grant_permission).

## Semantics

- Without `OF`, lists the permissions of every role. With `OF role_name`, lists the permissions of `role_name` and of every role granted to it, directly or through other roles. The `role` column shows the role that holds each grant.
- Without `ON`, lists permissions on every resource. With `ON resource`, lists permissions granted on that resource and on its parent resources, which also apply to it: `ON TABLE ks.t` includes grants on `KEYSPACE ks` and `ALL KEYSPACES`, `ON KEYSPACE ks` includes `ALL KEYSPACES`, and `ON ROLE r` includes `ALL ROLES`.
- With `NORECURSIVE`, the parent resources of the `ON` resource are not included. Grants inherited through role membership are always included.
- `LIST ALL` lists every permission; `LIST permission` lists only that permission. Unlike `GRANT`, the permission does not have to apply to the resource type: for example, `LIST SELECT ON ROLE r` is valid and lists nothing.
- The result has one row per permission, with the columns `role`, `username`, `resource`, and `permission` (all text). `username` is always equal to `role`, and is kept for compatibility with Apache Cassandra. Resources are shown as `<all keyspaces>`, `<keyspace ks>`, `<table ks.t>`, `<all roles>`, or `<role r>`.
- Rows are ordered by role, then resource, then permission in the order `CREATE`, `ALTER`, `DROP`, `SELECT`, `MODIFY`, `AUTHORIZE`, `DESCRIBE`.
- When no permission matches, the statement returns no result set (a void result), like Apache Cassandra.
- The role named in `OF`, and the keyspace, table, or role named in `ON`, must exist; otherwise an error is returned.
- The result is always returned in a single page, regardless of the fetch size.
- A superuser has every permission without explicit grants. Only permissions that were granted (explicitly, or automatically when the role created an object) are listed.

## Permissions

- A superuser, or a role that has `DESCRIBE` on `ALL ROLES` (directly or through a granted role), can list the permissions of every role.
- Any other role must use `OF`, and can list the permissions of its own roles (meaning itself and the roles granted to it) and of any role on which it has `DESCRIBE` (directly or through a granted role). The role that creates a role is granted `DESCRIBE` on it automatically. Otherwise, an unauthorized error is returned.

## Examples

```cql
cassandra@ycqlsh> CREATE KEYSPACE dev_keyspace;
cassandra@ycqlsh> CREATE TABLE dev_keyspace.tests (id int PRIMARY KEY, result boolean);
cassandra@ycqlsh> CREATE ROLE engineering;
cassandra@ycqlsh> CREATE ROLE developer;
cassandra@ycqlsh> GRANT engineering TO developer;
cassandra@ycqlsh> GRANT SELECT ON ALL KEYSPACES TO engineering;
cassandra@ycqlsh> GRANT MODIFY ON KEYSPACE dev_keyspace TO developer;
```

### List the permissions of a role, including inherited permissions

```cql
cassandra@ycqlsh> LIST ALL PERMISSIONS OF developer;
```

```output
 role        | username    | resource                | permission
-------------+-------------+-------------------------+------------
   developer |   developer | <keyspace dev_keyspace> |     MODIFY
 engineering | engineering |         <all keyspaces> |     SELECT

(2 rows)
```

### List who can read a table

```cql
cassandra@ycqlsh> LIST SELECT ON TABLE dev_keyspace.tests;
```

```output
 role        | username    | resource                   | permission
-------------+-------------+----------------------------+------------
   cassandra |   cassandra |    <keyspace dev_keyspace> |     SELECT
   cassandra |   cassandra | <table dev_keyspace.tests> |     SELECT
 engineering | engineering |            <all keyspaces> |     SELECT

(3 rows)
```

The `cassandra` rows are the permissions granted to `cassandra` when it created the keyspace and the table.

### List only the permissions granted on the resource itself

```cql
cassandra@ycqlsh> LIST ALL PERMISSIONS ON KEYSPACE dev_keyspace OF developer NORECURSIVE;
```

```output
 role      | username  | resource                | permission
-----------+-----------+-------------------------+------------
 developer | developer | <keyspace dev_keyspace> |     MODIFY

(1 rows)
```

## See also

- [GRANT PERMISSION](../ddl_grant_permission)
- [REVOKE PERMISSION](../ddl_revoke_permission)
- [GRANT ROLE](../ddl_grant_role)
- [LIST ROLES](../ddl_list_roles)
