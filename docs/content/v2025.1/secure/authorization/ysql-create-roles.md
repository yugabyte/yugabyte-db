## 1. Create roles

Create a role with a password. You can do this with the [CREATE ROLE](../../../api/ysql/the-sql-language/statements/dcl_create_role/) statement.

As an example, let us create a role `engineering` for an engineering team in an organization.

```plpgsql
yugabyte=# CREATE ROLE engineering;
```

Roles that have `LOGIN` privileges are users. As an example, you can create a user `john` as follows:

```plpgsql
yugabyte=# CREATE ROLE john LOGIN PASSWORD 'PasswdForJohn';
```

Read about [how to create users in YugabyteDB](../../enable-authentication/authentication-ysql/) in the Authentication section.

## 2. Grant roles

You can grant a role to another role (which can be a user), or revoke a role that has already been granted. Executing the `GRANT` and the `REVOKE` operations requires the `AUTHORIZE` privilege on the role being granted or revoked.

As an example, you can grant the `engineering` role you created above to the user `john` as follows:

```plpgsql
yugabyte=# GRANT engineering TO john;
```

Read more about [granting roles](../../../api/ysql/the-sql-language/statements/dcl_grant/).

## 3. Create a hierarchy of roles, if needed

In YSQL, you can create a hierarchy of roles. The privileges of any role in the hierarchy flows downward.

As an example, let us say that in the above example, you want to create a `developer` role that inherits all the privileges from the `engineering` role. You can achieve this as follows.

First, create the `developer` role.

```plpgsql
yugabyte=# CREATE ROLE developer;
```

Next, `GRANT` the `engineering` role to the `developer` role.

```plpgsql
yugabyte=# GRANT engineering TO developer;
```

## 4. List roles

You can list all the roles by running the following statement:

```plpgsql
yugabyte=# SELECT rolname, rolcanlogin, rolsuper, memberof FROM pg_roles;
```

You should see the following output:

```
 rolname     | rolcanlogin | rolsuper | memberof
-------------+-------------+----------+-----------------
 john        | t           | f        | {engineering}
 developer   | f           | f        | {engineering}
 engineering | f           | f        | {}
 yugabyte    | t           | t        | {}

(4 rows)
```

In the table above, note the following:

* The `yugabyte` role is the built-in superuser.
* The role `john` can log in, and hence is a user. Note that `john` is not a superuser.
* The roles `engineering` and `developer` cannot log in.
* Both `john` and `developer` inherit the role `engineering`.

## 5. Revoke roles

Roles can be revoked using the [REVOKE](../../../api/ysql/the-sql-language/statements/dcl_revoke/) statement.

In the above example, you can revoke the `engineering` role from the user `john` as follows:

```plpgsql
yugabyte=# REVOKE engineering FROM john;
```

Listing all the roles now shows that `john` no longer inherits from the `engineering` role:

```plpgsql
yugabyte=# SELECT rolname, rolcanlogin, rolsuperuser, memberof FROM pg_roles;
```

```
 rolname     | rolcanlogin | rolsuper | memberof
-------------+-------------+----------+-----------------
john         | t           | f        | {}
developer    | f           | f        | {engineering}
engineering  | f           | f        | {}
yugabyte     | t           | t        | {}

(4 rows)
```

## 6. Drop roles

Roles can be dropped with the [DROP ROLE](../../../api/ysql/the-sql-language/statements/dcl_drop_role/) statement.

In the above example, you can drop the `developer` role with the following statement:

```plpgsql
yugabyte=# DROP ROLE developer;
```

The `developer` role would no longer be present upon listing all the roles:

```plpgsql
yugabyte=# SELECT rolname, rolcanlogin, rolsuper, memberof FROM pg_roles;
```

```
 rolname     | rolcanlogin | rolsuper | memberof
-------------+-------------+----------+-----------
 john        | t           | f        | {}
 engineering | f           | f        | {}
 yugabyte    | t           | t        | {}

(3 rows)
```
