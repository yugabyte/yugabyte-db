---
title: Add database users
linkTitle: Add database users
description: Add users to Yugabyte Cloud clusters
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: add-users
    parent: cloud-connect
    weight: 60
isTocNested: true
showAsideToc: true
---

Create database users to provide clients access to the cluster's YugabyteDB database. A database user's access is determined by the roles assigned to the user. YugabyteDB uses role-based access control (RBAC) to [manage authorization](../../cloud-security/cloud-users/).

Once you have added a user to the database, send them the credentials.

You will also have to authorize their network so that they can access the cluster. Refer to [Assign IP allow lists](../../cloud-basics/add-connections/).

## Create a database user

When creating a YugabyteDB cluster in Yugabyte Cloud, you set up the credentials for your admin user. To allow other team members to access the database, you can add additional database users.

To add a database user:

1. Connect to the cluster via [cloud shell](../connect-cloud-shell/) using `ysqlsh` or `ycqlsh`.

1. Add a user using the `CREATE ROLE` statement.

    ```sql
    yugabyte=# CREATE ROLE <username> WITH LOGIN PASSWORD '<password>';
    ```

    ```sql
    admin@ycqlsh> CREATE ROLE <username> WITH PASSWORD = '<password>' AND LOGIN = true;
    ```

To add or change a password for a user, use the `ALTER ROLE` statement.

## Learn more

- [Manage Users and Roles in YugabyteDB](../../../secure/authorization/create-roles/)

- [Database authorization in Yugabyte Cloud clusters](../../cloud-security/cloud-users/)

## Next steps

- [Connect an application](../connect-applications/)
