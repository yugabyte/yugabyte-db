---
title: 1. RBAC Model
linkTitle: 1. RBAC Model
description: 1. RBAC Model
headcontent: How role-based access control works in YCQL
image: /images/section_icons/secure/rbac-model.png
aliases:
  - /secure/authorization/rbac-model/
menu:
  v1.1:
    identifier: secure-authorization-rbac-model
    parent: secure-authorization
    weight: 716
isTocNested: true
showAsideToc: true
---

The role-based access control model in YCQL is a collection of permissions on resources given to roles. Thus, the entire RBAC model is built around **roles**, **resources** and **permissions**. It is essential to understand these concepts in order to understand the RBAC model.

## Roles

Roles in YCQL can represent individual users or a group of users. They encapsulate a set of privileges that can be assigned to other roles (or users). Roles are essential to implementing and administering access control on a YugaByte DB cluster. Below are some important points about roles:

* Roles which have login privilege are users. Hence, all users are roles but all roles are not users.

* Roles can be granted to other roles, making it possible to organize roles into a hierarchy.

* Roles inherit the permissions of all other roles granted to them.


## Resources

YCQL defines a number of specific resources, that represent underlying database objects. A resource can denote one object or a collection of objects. YCQL resources are hierarchical as described below:

* Keyspaces and tables follow the hierarchy: `ALL KEYSPACES` > `KEYSPACE` > `TABLE`
* ROLES are hierarchical (they can be assigned to other roles). They follow the hierarchy: `ALL ROLES` > `ROLE #1` > `ROLE #2` ...

The table below lists out the various resources.

Resource        | Description |
----------------|-------------|
`KEYSPACE`      | Denotes one keyspace. Typically includes all the tables and indexes defined in that keyspace. |
`TABLE`         | Denotes one table. Includes all the indexes defined on that table. |
`ROLE`          | Denotes one role. |
`ALL KEYSPACES` | Collection of all keyspaces in the database. |
`ALL ROLES`     | Collection of all roles in the database. |



## Permissions

Permissions are necessary to execute operations on database objects. Permissions can be granted at any level of the database hierarchy and are inherited downwards. The set of permissions include:


Permission  | Objects                      | Operations                          |
------------|------------------------------|-------------------------------------|
`ALTER`     | keyspace, table, role        | ALTER                               |
`AUTHORIZE` | keyspace, table, role        | GRANT PERMISSION, REVOKE PERMISSION |
`CREATE`    | keyspace, table, role, index | CREATE                              |
`DROP`      | keyspace, table, role, index | DROP                                |
`MODIFY`    | keyspace, table              | INSERT, UPDATE, DELETE, TRUNCATE    |
`SELECT`    | keyspace, table              | SELECT                              |
`DESCRIBE` (not implemented)  | role       | LIST ROLES                          |

{{< note title="Note" >}}
The `ALTER` permission on the base table is required in order to `CREATE` or `DROP` indexes on it.
{{< /note >}}

Read more about [YCQL permissions](../../api/ycql/ddl_grant_permission/#permissions).
