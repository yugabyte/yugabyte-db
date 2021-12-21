---
title: Create YSQL extensions in Yugabyte Cloud
linkTitle: Create extensions
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

Extend the functionality of your cluster using PostgreSQL extensions. YugabyteDB comes bundled with a number of extensions that are tested to work with YSQL. For a list of bundled extensions, refer to [Pre-bundled extensions](../../../explore/ysql-language-features/extensions).

## Loading extensions

Before using an extension, you must load it in the database using the [CREATE EXTENSION](../../../api/ysql/the-sql-language/statements/ddl_create_extension/) command. For example:

```sql
CREATE EXTENSION fuzzystrmatch;
```

## Required privileges

In Yugabyte Cloud, extensions can only be loaded by users that are a member of the `yb_extension` role. All `yb_superuser` users, including the default admin user, are members of `yb_extension`.

Use the `GRANT` statement to assign the role to users. For example, to grant the `yb_extension` role to `user`, use the following command:

```sql
yugabyte=# GRANT yb_extension TO user;
```

For more information on roles in Yugabyte Cloud, refer to [Database authorization in Yugabyte Cloud clusters](../../cloud-security/cloud-users/).

## Request support for a new extension

You cannot install new extensions in Yugabyte Cloud.

If you need a database extension that is not bundled with YugabyteDB added to a cluster, contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431) with the names of the cluster and extension, or [reach out on Slack](https://yugabyte-db.slack.com/).

## Learn more

- [Pre-bundled extensions](../../../explore/ysql-language-features/extensions/)
- [Install and use extensions](../../../api/ysql/extensions/)
- [Database authorization in Yugabyte Cloud clusters](../../cloud-security/cloud-users/)
- [Manage Users and Roles in YSQL](../../../secure/authorization/create-roles/)
