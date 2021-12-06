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

Create database users to provide clients access to the cluster's YugabyteDB database. A database user's access is determined by the roles assigned to the user. YugabyteDB uses role-based access control (RBAC) to manage authorization.

Once you have added a user to the database, send them the credentials.

You will also have to authorize their network so that they can access the cluster. Refer to [Assign IP allow lists](../../cloud-basics/add-connections/).

## Create a database user

When a cluster is added, an admin user is created for the database. You must add additional database users for any other users who require access the cluster database.

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

For more information on managing users in YSQL, refer to [Enable users in YSQL](../../../secure/enable-authentication/ysql/).

For more information on managing users in YCQL, refer to [Enable users in YCQL](../../../secure/enable-authentication/ycql/).

## Next steps

- [Connect an application](../connect-applications/)
