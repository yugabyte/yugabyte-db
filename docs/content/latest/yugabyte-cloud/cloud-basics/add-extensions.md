---
title: Add YSQL extensions
linkTitle: Add extensions
description: Add extensions to a Yugabyte Cloud cluster.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: add-extensions
    parent: cloud-basics
    weight: 50
isTocNested: true
showAsideToc: true
---

Extend the functionality of your cluster using PostgreSQL extensions. For a list of extensions supported by YugabyteDB, refer to [Pre-bundled extensions](../../../explore/ysql-language-features/extensions).

## Required privileges

In Yugabyte Cloud, extensions can only be created by users that are part of the `yb_extension` role. Use the `GRANT` statement to assign the role to users. For example, to grant the `yb_extension` role to `admin`, use the following command:

```sql
yugabyte=# GRANT yb_extension TO admin;
```

For more information on roles and privelges in YSQL, refer to [Manage Users and Roles in YSQL](../../../secure/authorization/create-roles/).

## Installing an extension

Before using an extension, install it using the [CREATE EXTENSION](../../../api/ysql/the-sql-language/statements/ddl_create_extension/) command. For example:

```sql
CREATE EXTENSION fuzzystrmatch;
```

## Next steps

- [Connect to your cluster](../../cloud-connect/)
- [Create a database](../../cloud-connect/create-databases)
- [Add database users](../../cloud-connect/add-users/)
