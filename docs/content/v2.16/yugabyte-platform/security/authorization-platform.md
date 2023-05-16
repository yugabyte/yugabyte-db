---
title: Authorization
headerTitle: Authorization
linkTitle: Authorization
description: Use the role-based access control (RBAC) model in YugabyteDB Anywhere to manage users and roles.
menu:
  v2.16_yugabyte-platform:
    parent: security
    identifier: authorization-platform
    weight: 27
type: docs
---

The role-based access control (RBAC) model in YugabyteDB Anywhere is a collection of privileges on resources given to roles. Thus, the entire RBAC model is built around roles, resources, and privileges. It is essential to understand these concepts in order to understand the RBAC model.

## Users

A user can interact with a YugabyteDB Anywhere through the UI or REST API.

## Roles

A role is a set of predefined permissions within YugabyteDB Anywhere. The following roles are available:

* **Super Admin** is the first user that is created during the tenant registration. This role has the highest level of privilege that allows all read and write actions on all YugabyteDB Anywhere resources. There can be only one Super Admin in a tenant. This Super Admin can perform the following:

  * Manage all resources, including universes, nodes, backup and restore, and cloud providers.
  * Manage the user access control by creating and managing users.

  For more information, see [admin user](../../configure-yugabyte-platform/create-admin-user/).
* **Admin** has privileges that are similar to the Super Admin, except that the Admin cannot manage the global scope artifacts and actions, such as runtime configuration settings and LDAP authentication.
* **Backup Admin** has access the backup related tasks, such as the following:

  * Manage database backups and restore operations.
  * Create new backups.
  * Delete any existing backup or backup schedule.
  * Edit existing backups.
  * Read-only permissions for all other resources in YugabyteDB Anywhere.
* **Read-only** access level provides viewer permission to the UI and API.

## Create, modify, and delete users

As a Super Admin or Admin, you can invite new users and manage existing users for your tenant.

You can invite new users to your tenant as follows:

* Navigate to **Admin > User Management > Users** and click **Add User**.

* Complete the fields of the **Add User** dialog shown in the following illustration, and then click **Submit**:

  ![Add User](/images/yp/authorization-platform/add-user.png)

To modify a user role or delete the user, navigate to **Admin > User Management > Users**. Click **Actions** that corresponds to the specific user, and then select either **Edit User Role** or **Delete User**.
