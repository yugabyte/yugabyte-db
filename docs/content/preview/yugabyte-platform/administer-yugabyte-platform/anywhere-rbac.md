---
title: Manage access to YugabyteDB Anywhere
headerTitle: Manage YugabyteDB Anywhere users
linkTitle: Manage users
description: Use role-based access control (RBAC) in YugabyteDB Anywhere to manage users and roles.
headcontent: Invite team members to your account and manage their access
menu:
  preview_yugabyte-platform:
    parent: administer-yugabyte-platform
    identifier: anywhere-rbac
    weight: 27
type: docs
---

YugabyteDB Anywhere uses a role-based access control (RBAC) model to manage access to your YugabyteDB Anywhere instance (whether via the UI or the REST API). Using roles, you can enforce the [principle of least privilege](https://en.wikipedia.org/wiki/Principle_of_least_privilege) (PoLP) by ensuring that users have the precise permissions needed to fulfill their roles while mitigating the risk of unauthorized access or accidental breaches. A role defines a set of permissions that determine what features can be accessed by account users who have been assigned that role.

## Users

A user can interact with a YugabyteDB Anywhere through the UI or REST API.

Users are assigned roles, which determine what they actions they can perform.

### Create, modify, and delete users

As a Super Admin or Admin, you can invite new users and manage existing users for your YugabyteDB Anywhere instance.

You can invite new users as follows:

1. Navigate to **Admin > User Management > Users**, and click **Add User**.

1. Complete the fields of the **Add User** dialog shown in the following illustration:

    ![Add User](/images/yp/authorization-platform/add-user.png)

1. Click **Submit**.

To modify a user role or delete the user, navigate to **Admin > User Management > Users**. Click **Actions** that corresponds to the specific user, and then choose either **Edit User Role** or **Delete User**.

## Roles

YugabyteDB Anywhere includes built-in roles, and you can define custom roles for team members to restrict access to specific account features.

[API tokens](../../anywhere-automation/) generated for users are assigned the same role as the user that generated them.

### Built-in roles

The following built-in roles are available:

* **Super Admin** is the first user that is created during the installation. This role has the highest level of privilege that allows all read and write actions on all YugabyteDB Anywhere resources. There can be only one Super Admin. This Super Admin can perform the following:

  * Manage all resources, including universes, nodes, backup, restore, and cloud providers.
  * Manage the user access control by creating and managing users.

  For more information, see [admin user](../../configure-yugabyte-platform/create-admin-user/).

* **Admin** has privileges that are similar to the Super Admin, except that the Admin cannot manage the global scope artifacts and actions, such as runtime configuration settings and LDAP authentication.

* **Backup Admin** has access to the backup related tasks, such as the following:

  * Manage database backups and restore operations.
  * Create new backups.
  * Delete any existing backup or backup schedule.
  * Edit existing backups.
  * Read-only permissions for all other resources in YugabyteDB Anywhere.

* **Read-only** access level provides viewer permission to the UI and API.

### Create, modify, and delete roles

As a Super Admin or Admin, you can create and modify roles.

To create a custom role, do the following:

1. Navigate to **Admin > User Management > Roles**, then click **Add Role** to display the **Create a Role** dialog.
1. Enter a name for the role.
1. Enter a description for the role.
1. Click **Select Permissions**.
1. Select the permissions to assign to the role and click **Select** when you are done.
1. Click **Save**.

To modify a user role or delete a role, navigate to **Admin > User Management > Roles**. Click **Actions** that corresponds to the specific role, and then select either **Edit Role** or **Delete Role**.
