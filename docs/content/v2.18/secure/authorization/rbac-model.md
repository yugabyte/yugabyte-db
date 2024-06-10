---
title: Role-based access control (RBAC) model in YSQL
linkTitle: Overview
headerTitle: Role-based access overview
description: Overview of the role-based access control (RBAC) model in YSQL.
headcontent: How role-based access control works
menu:
  v2.18:
    identifier: rbac-model
    parent: authorization
    weight: 716
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../rbac-model/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../rbac-model-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

The role-based access control (RBAC) model in YSQL is a collection of privileges on resources given to roles. Thus, the entire RBAC model is built around roles, resources, and privileges. It is essential to understand these concepts in order to understand the RBAC model.

## Roles

Roles in YSQL can represent individual users or a group of users. They encapsulate a set of privileges that can be assigned to other roles (or users). Roles are essential to implementing and administering access control on a YugabyteDB cluster. Below are some important points about roles:

* Roles which have `LOGIN` privilege are users. Hence, all users are roles, but not all roles are users.

* Roles can be granted to other roles, making it possible to organize roles into a hierarchy.

* Roles inherit the privileges of all other roles granted to them.

YugabyteDB inherits a number of roles from PostgreSQL, including the `postgres` user, and adds several new roles. View the YugabyteDB-specific roles for your clusters with the following command (or use `\duS` to display all roles):

```sql
yugabyte=> \du
```

```output
                                     List of roles
  Role name   |                         Attributes                         | Member of
--------------+------------------------------------------------------------+-----------
 postgres     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
 yb_extension | Cannot login                                               | {}
 yb_fdw       | Cannot login                                               | {}
 yugabyte     | Superuser, Create role, Create DB, Replication, Bypass RLS | {}
```

The following table describes the default YSQL roles and users in YugabyteDB clusters.
<!-- Portions of this table are also under Database authorization in Yugabyte cloud -->

| Role | Description |
| :--- | :---------- |
| postgres | Superuser role created during database creation. |
| yb_extension | Role that allows non-superuser users to create PostgreSQL extensions. |
| yb_fdw | Role that allows non-superuser users to [CREATE](../../../api/ysql/the-sql-language/statements/ddl_create_foreign_data_wrapper/), [ALTER](../../../api/ysql/the-sql-language/statements/ddl_alter_foreign_data_wrapper/), and [DROP](../../../api/ysql/the-sql-language/statements/ddl_drop_foreign_data_wrapper/) [foreign data wrappers](../../../explore/ysql-language-features/advanced-features/foreign-data-wrappers/). |
| yugabyte | Superuser role used during database creation, by Yugabyte support to perform maintenance operations, and for backups (using ysql_dump). |

### yb_extension

The `yb_extension` role allows non-superuser roles to [create extensions](../../../api/ysql/the-sql-language/statements/ddl_create_extension/). A user granted this role can create all the extensions that are bundled in YugabyteDB.

Create a role `test` and grant `yb_extension` to this role.

```sql
yugabyte=# create role test;
yugabyte=# grant yb_extension to test;
yugabyte=# set role test;
yugabyte=> select * from current_user;
```

```output
 current_user
--------------
 test
(1 row)
```

Create an extension as the test user and check if it's created.

```sql
yugabyte=> create extension pgcrypto;
yugabyte=> select * from pg_extension where extname='pgcrypto';
```

```output
 extname  | extowner | extnamespace | extrelocatable | extversion | extconfig | extcondition
----------+----------+--------------+----------------+------------+-----------+--------------
 pgcrypto |    16386 |         2200 | t              | 1.3        |           |
(1 row)
```

## Resources

YSQL defines a number of specific resources that represent underlying database objects. A resource can represent one object or a collection of objects. YSQL resources are hierarchical as described below:

* Databases and tables follow the hierarchy: `ALL DATABASES` > `DATABASE` > `TABLE`
* ROLES are hierarchical (they can be assigned to other roles). They follow the hierarchy: `ALL ROLES` > `ROLE #1` > `ROLE #2` ...

The table below lists out the various resources.

Resource        | Description |
----------------|-------------|
`DATABASE`      | Denotes one database. Typically includes all the tables and indexes defined in that database. |
`TABLE`         | Denotes one table. Includes all the indexes defined on that table. |
`ROLE`          | Denotes one role. |
`ALL DATABASES` | Collection of all databases in the database. |
`ALL ROLES`     | Collection of all roles in the database. |

## Privileges

Privileges are necessary to execute operations on database objects. Privileges can be granted at any level of the database hierarchy and are inherited downwards. The set of privileges include:

Privilege  | Objects                      | Operations                          |
------------|------------------------------|-------------------------------------|
`ALTER`     | database, table, role        | ALTER                               |
`AUTHORIZE` | database, table, role        | GRANT privilege, REVOKE privilege |
`CREATE`    | database, table, role, index | CREATE                              |
`DROP`      | database, table, role, index | DROP                                |
`MODIFY`    | database, table              | INSERT, UPDATE, DELETE, TRUNCATE    |
`SELECT`    | database, table              | SELECT                              |

{{< note title="Note" >}}

The `ALTER TABLE` privilege on the base table is required in order to CREATE or DROP indexes on it.

{{< /note >}}

Read more about [YSQL privileges](../../../api/ysql/the-sql-language/statements/dcl_grant/).
