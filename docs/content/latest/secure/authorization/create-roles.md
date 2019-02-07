---
title: 2. Create Roles
linkTitle: 2. Create Roles
description: 2. Create Roles
headcontent: Creating roles in YCQL
image: /images/section_icons/secure/create-roles.png
aliases:
  - /secure/authorization/create-roles/
menu:
  latest:
    identifier: secure-authorization-create-roles
    parent: secure-authorization
    weight: 717
isTocNested: true
showAsideToc: true
---

## 1. Create roles

Create a role with a password. You can do this with the [CREATE ROLE](/api/cassandra/ddl_create_role/) command.


As an example, let us create a role `engineering` for an engineering team in an organization. Note that we add the `IF NOT EXISTS` clause in case the role already exists.
<div class='copy separator-gt'>
```sql
cassandra@cqlsh> CREATE ROLE IF NOT EXISTS engineering;
```
</div>

Roles that have `LOGIN` privileges are users. As an example, you can create a user `john` as follows:
<div class='copy separator-gt'>
```sql
cassandra@cqlsh> CREATE ROLE IF NOT EXISTS john WITH PASSWORD = 'PasswdForJohn' AND LOGIN = true;
```
</div>

Read about [how to create users in YugaByte DB](/secure/authentication/) in the authentication section.


## 2. Grant roles

You can grant a role to another role (which can be a user), or revoke a role that has already been granted. Executing the `GRANT` and the `REVOKE` operations requires the `AUTHORIZE` permission on the role being granted or revoked.

As an example, you can grant the `engineering` role we created above to the user `john` as follows:
<div class='copy separator-gt'>
```sql
cassandra@cqlsh> GRANT engineering TO john;
```
</div>

Read more about [granting roles](/api/cassandra/ddl_grant_role/).


## 3. Create a hierarchy of roles if needed

In YCQL, you can create a hierarchy of roles. The permissions of any role in the hierarchy flows downward.

As an example, let us say that in the above example, we want to create a `developer` role that inherits all the permissions from the `engineering` role. You can achieve this as follows.

First, create the `developer` role.
<div class='copy separator-gt'>
```sql
cassandra@cqlsh> CREATE ROLE IF NOT EXISTS developer;
```
</div>

Next, `GRANT` the `engineering` role to the `developer` role.
<div class='copy separator-gt'>
```sql
cassandra@cqlsh> GRANT engineering TO developer;
```
</div>


## 3. List roles

You can list all the roles by running the following command:
<div class='copy separator-gt'>
```sql
cassandra@cqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles;
```
</div>

You should see the following output:
```
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
* The role `john` can login, and hence is a user. Note that `john` is not a superuser.
* The roles `engineering` and `developer` cannot login.
* Both `john` and `developer` inherit the role `engineering`.


## 5. Revoke roles

Roles can be revoked using the [REVOKE ROLE](/api/cassandra/ddl_revoke_role/) command.

In the above example, we can revoke the `engineering` role from the user `john` as follows:
<div class='copy separator-gt'>
```sql
cassandra@cqlsh> REVOKE engineering FROM john;
```
</div>

Listing all the roles now shows that `john` no longer inherits from the `engineering` role:

```{.sql}
cassandra@cqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles;

 role        | can_login | is_superuser | member_of
-------------+-----------+--------------+-----------------
        john |      True |        False |                []
   developer |     False |        False | ['engineering']
 engineering |     False |        False |                []
   cassandra |      True |         True |                []

(4 rows)
```


## 6. Drop roles

Roles can be dropped with the [DROP ROLE](/api/cassandra/ddl_drop_role/) command.

In the above example, we can drop the `developer` role with the following command:
<div class='copy separator-gt'>
```sql
cassandra@cqlsh> DROP ROLE IF EXISTS developer;
```
</div>

The `developer` role would no longer be present upon listing all the roles:
```{.sql}
cassandra@cqlsh> SELECT role, can_login, is_superuser, member_of FROM system_auth.roles;

 role        | can_login | is_superuser | member_of
-------------+-----------+--------------+-----------
        john |      True |        False |          []
 engineering |     False |        False |          []
   cassandra |      True |         True |          []

(3 rows)
```
