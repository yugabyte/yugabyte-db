---
title: Manage users and roles
linkTitle: Manage users and roles
description: Manage users and roles in YSQL
headcontent: Manage users and roles
menu:
  v2.16:
    identifier: create-roles
    parent: authorization
    weight: 717
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../create-roles/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../create-roles-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

Roles in YSQL can represent individual users or a group of users. Users are a role that has login permissions.

You manage roles and users using the CREATE ROLE, GRANT, REVOKE, and DROP ROLE statements.

{{< note title="YSQL and case sensitivity" >}}

Like SQL, YSQL is case-insensitive by default. When specifying an identifier, such as the name of a table or role, YSQL automatically converts the identifier to lowercase. For example, `CREATE ROLE Alice` creates the role "alice". To use a case-sensitive name for an identifier, enclose the name in quotes. For example, to create the role "Alice", use `CREATE ROLE "Alice"`.

{{< /note >}}

## Create roles

You can create roles with the [CREATE ROLE](../../../api/ysql/the-sql-language/statements/dcl_create_role/) statement.

For example, to create a role `engineering` for an engineering team in an organization, do the following:

```sql
yugabyte=# CREATE ROLE engineering;
```

Roles that have `LOGIN` privileges are users. For example, create a user `john` as follows:

```sql
yugabyte=# CREATE ROLE john LOGIN PASSWORD 'PasswdForJohn';
```

Read about [how to create users in YugabyteDB](../../enable-authentication/ysql/) in the Authentication section.

## Grant roles

You can grant a role to another role (which can be a user), or revoke a role that has already been granted. Executing the [GRANT](../../../api/ysql/the-sql-language/statements/dcl_grant/) and the [REVOKE](../../../api/ysql/the-sql-language/statements/dcl_revoke/) operations requires the `AUTHORIZE` privilege on the role being granted or revoked.

For example, you can grant the `engineering` role you created above to the user `john` as follows:

```sql
yugabyte=# GRANT engineering TO john;
```

Read more about [granting privileges](../ysql-grant-permissions/).

## Create a hierarchy of roles

In YSQL, you can create a hierarchy of roles. The privileges of any role in the hierarchy flows downward.

For example, you can create a `developer` role that inherits all the privileges from the `engineering` role.

First, create the `developer` role.

```sql
yugabyte=# CREATE ROLE developer;
```

Next, `GRANT` the `engineering` role to the `developer` role.

```sql
yugabyte=# GRANT engineering TO developer;
```

## List roles

You can list all the roles by running the following statement:

```sql
yugabyte=# SELECT rolname, rolcanlogin, rolsuper, memberof FROM pg_roles;
```

You should see the following output:

```output
 rolname     | rolcanlogin | rolsuper | memberof
-------------+-------------+----------+-----------------
 john        | t           | f        | {engineering}
 developer   | f           | f        | {engineering}
 engineering | f           | f        | {}
 yugabyte    | t           | t        | {}

(4 rows)
```

In the table, note the following:

* The `yugabyte` role is the built-in superuser.
* The role `john` can log in, and hence is a user. Note that `john` is not a superuser.
* The roles `engineering` and `developer` cannot log in.
* Both `john` and `developer` inherit the role `engineering`.

## Revoke roles

Revoke roles using the [REVOKE](../../../api/ysql/the-sql-language/statements/dcl_revoke/) statement.

For example, you can revoke the `engineering` role from the user `john` as follows:

```sql
yugabyte=# REVOKE engineering FROM john;
```

Listing all the roles now shows that `john` no longer inherits from the `engineering` role:

```sql
yugabyte=# SELECT rolname, rolcanlogin, rolsuperuser, memberof FROM pg_roles;
```

```output
 rolname     | rolcanlogin | rolsuper | memberof
-------------+-------------+----------+-----------------
john         | t           | f        | {}
developer    | f           | f        | {engineering}
engineering  | f           | f        | {}
yugabyte     | t           | t        | {}

(4 rows)
```

## Drop roles

Drop roles using the [DROP ROLE](../../../api/ysql/the-sql-language/statements/dcl_drop_role/) statement.

For example, you can drop the `developer` role with the following statement:

```sql
yugabyte=# DROP ROLE developer;
```

The `developer` role is no longer present when listing all the roles:

```sql
yugabyte=# SELECT rolname, rolcanlogin, rolsuper, memberof FROM pg_roles;
```

```output
 rolname     | rolcanlogin | rolsuper | memberof
-------------+-------------+----------+-----------
 john        | t           | f        | {}
 engineering | f           | f        | {}
 yugabyte    | t           | t        | {}

(3 rows)
```
