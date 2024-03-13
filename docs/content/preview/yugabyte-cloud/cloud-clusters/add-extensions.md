---
title: Create YSQL extensions
linkTitle: Create extensions
description: Add extensions to a YugabyteDB Managed cluster.
headcontent: Extend the functionality of your cluster using PostgreSQL extensions
menu:
  preview_yugabyte-cloud:
    identifier: add-extensions
    parent: cloud-clusters
    weight: 400
type: docs
---

YugabyteDB includes a number of [pre-bundled PostgreSQL extensions](../../../explore/ysql-language-features/pg-extensions/), tested to work with YSQL, that you can use to extend the functionality of your database.

{{< note title="Extensions must be pre-bundled" >}}

YugabyteDB Managed only supports extensions that are pre-bundled in YugabyteDB. You cannot install new extensions in YugabyteDB Managed. Refer to [PostgreSQL extensions](../../../explore/ysql-language-features/pg-extensions/) for the list of pre-bundled extensions.

{{< /note >}}

## Loading extensions

Before using an extension, you must load it in the database using the [CREATE EXTENSION](../../../api/ysql/the-sql-language/statements/ddl_create_extension/) command. For example:

```sql
CREATE EXTENSION fuzzystrmatch;
```

## Required privileges

In YugabyteDB Managed, extensions can only be loaded by users that are a member of the `yb_extension` role. All `yb_superuser` users, including the default database admin user, are members of `yb_extension`.

Use the `GRANT` statement to assign the role to users. For example, to grant the `yb_extension` role to `user`, use the following command:

```sql
yugabyte=# GRANT yb_extension TO user;
```

For more information on roles in YugabyteDB Managed, refer to [Database authorization in YugabyteDB Managed clusters](../../cloud-secure-clusters/cloud-users/).

## Learn more

- [PostgreSQL extensions](../../../explore/ysql-language-features/pg-extensions/)
- [Database authorization in YugabyteDB Managed clusters](../../cloud-secure-clusters/cloud-users/)
- [Manage Users and Roles in YSQL](../../../secure/authorization/create-roles/)
