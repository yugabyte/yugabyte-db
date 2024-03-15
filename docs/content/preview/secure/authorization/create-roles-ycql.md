---
title: Manage users and roles - YCQL
headerTitle: Manage users and roles
linkTitle: Manage users and roles
description: Manage users and roles in YCQL
headcontent: Manage users and roles
menu:
  preview:
    identifier: create-roles-ycql
    parent: authorization
    weight: 717
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../create-roles/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../create-roles-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

Roles in YCQL can represent individual users or a group of users. Users are a role that has login permissions.

You manage roles and users using the CREATE ROLE, GRANT ROLE, REVOKE ROLE, and DROP ROLE statements.

{{< note title="YCQL and case sensitivity" >}}

Like CQL, YCQL is case-insensitive by default. When specifying an identifier, such as the name of a table or role, YCQL automatically converts the identifier to lowercase. For example, `CREATE ROLE Alice` creates the role "alice". To use a case-sensitive name for an identifier, enclose the name in quotes. For example, to create the role "Alice", use `CREATE ROLE "Alice"`.

{{< /note >}}

## Create roles

You can create roles with the [CREATE ROLE](../../../api/ycql/ddl_create_role/) command.

For example, to create a role `engineering` for an engineering team in an organization, do the following (add the `IF NOT EXISTS` clause in case the role already exists):

```cql
cassandra@ycqlsh> CREATE ROLE IF NOT EXISTS engineering;
```

Roles that have `LOGIN` permissions are users. For example, create a user `john` as follows:

```cql
cassandra@ycqlsh> CREATE ROLE IF NOT EXISTS john WITH PASSWORD = 'PasswdForJohn' AND LOGIN = true;
```

Read about [how to create users in YugabyteDB](../../enable-authentication/ycql/) in the authentication section.

## Grant roles

You can grant a role to another role (which can be a user), or revoke a role that has already been granted. Executing the [GRANT ROLE](../../../api/ycql/ddl_grant_role/) and the [REVOKE ROLE](../../../api/ycql/ddl_revoke_role/) operations requires the `AUTHORIZE` permission on the role being granted or revoked.

For example, you can grant the `engineering` role you created above to the user `john` as follows:

```cql
cassandra@ycqlsh> GRANT engineering TO john;
```

Read more about [granting privileges](../ycql-grant-permissions/).

## Create a hierarchy of roles

In YCQL, you can create a hierarchy of roles. The permissions of any role in the hierarchy flows downward.

For example, you can create a `developer` role that inherits all the permissions from the `engineering` role.

First, create the `developer` role.

```cql
cassandra@ycqlsh> CREATE ROLE IF NOT EXISTS developer;
```

Next, `GRANT` the `engineering` role to the `developer` role.

```cql
cassandra@ycqlsh> GRANT engineering TO developer;
```

## List roles

You can list all the roles by running the following command:

```cql
cassandra@ycqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles;
```

You should see the following output:

```output
 role        | can_login | is_superuser | member_of
-------------+-----------+--------------+-----------------
        john |      True |        False | ['engineering']
   developer |     False |        False | ['engineering']
 engineering |     False |        False |                []
   cassandra |      True |         True |                []

(4 rows)
```

In the table above, note the following:

* The `cassandra` role is the built-in superuser.
* The role `john` can log in, and hence is a user. Note that `john` is not a superuser.
* The roles `engineering` and `developer` cannot log in.
* Both `john` and `developer` inherit the role `engineering`.

## Revoke roles

Revoke roles using the [REVOKE ROLE](../../../api/ycql/ddl_revoke_role/) command.

For example, you can revoke the `engineering` role from the user `john` as follows:

```cql
cassandra@ycqlsh> REVOKE engineering FROM john;
```

Listing all the roles now shows that `john` no longer inherits from the `engineering` role:

```cql
cassandra@ycqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles;
```

```output
 role        | can_login | is_superuser | member_of
-------------+-----------+--------------+-----------------
        john |      True |        False |                []
   developer |     False |        False | ['engineering']
 engineering |     False |        False |                []
   cassandra |      True |         True |                []

(4 rows)
```

## Drop roles

Drop roles using the [DROP ROLE](../../../api/ycql/ddl_drop_role/) command.

For example, you can drop the `developer` role with the following command:

```cql
cassandra@ycqlsh> DROP ROLE IF EXISTS developer;
```

The `developer` role is no longer present when listing all the roles:

```cql
cassandra@ycqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles;
```

```output
 role        | can_login | is_superuser | member_of
-------------+-----------+--------------+-----------
        john |      True |        False |          []
 engineering |     False |        False |          []
   cassandra |      True |         True |          []

(3 rows)
```
