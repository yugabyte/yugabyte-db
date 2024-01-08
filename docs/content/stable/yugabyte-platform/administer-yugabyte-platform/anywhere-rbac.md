---
title: Manage access to YugabyteDB Anywhere
headerTitle: Manage YugabyteDB Anywhere users
linkTitle: Manage users
description: Use role-based access control (RBAC) in YugabyteDB Anywhere to manage users and roles.
headcontent: Invite team members to your account and manage their access
earlyAccess: /preview/releases/versioning/#feature-availability
menu:
  stable_yugabyte-platform:
    parent: administer-yugabyte-platform
    identifier: anywhere-rbac
    weight: 10
type: docs
---

YugabyteDB Anywhere uses a role-based access control (RBAC) model to manage access to your YugabyteDB Anywhere instance (whether via the UI or the REST API). Using roles, you can enforce the [principle of least privilege](https://en.wikipedia.org/wiki/Principle_of_least_privilege) (PoLP) by ensuring that users have the precise permissions needed to fulfill their roles while mitigating the risk of unauthorized access or accidental breaches. A role defines a set of permissions that determine what features can be accessed by account users who have been assigned that role.

{{< note title="Early Access">}}

RBAC with the ability to limit users to manage only a subset of universes is [Early Access](/preview/releases/versioning/#feature-availability).

By default, RBAC is not enabled. To enable the feature, use following API command:

```sh
curl --request PUT \
  --url http://{yba_host:port}/api/v1/customers/{customerUUID}/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.rbac.use_new_authz \
  --header 'Content-Type: text/plain' \
  --header 'X-AUTH-YW-API-TOKEN: {api_token}' \
  --data 'true'
```

Note that if you enable RBAC, you can't turn it off. You should test the feature thoroughly in a development or staging environment before enabling it in your production environment.

{{</note >}}

## Users

A user can interact with a YugabyteDB Anywhere through the UI or REST API.

Users are assigned [roles](#roles), which define the set of actions users can perform. You can also define a set of universes to which the user has access.

### Create, modify, and delete users

As a Super Admin or Admin, you can invite new users and manage existing users for your YugabyteDB Anywhere instance.

You can assign built-in and custom roles to users to determine the actions they are allowed to perform, and specify the universes that they can access.

To create a user, do the following:

1. Navigate to **Admin > Access Management > Users**, and click **Create User**.

    ![Add User](/images/yp/authorization-platform/add-user.png)

1. Enter the user's email.
1. To assign a built-in role, under **Built-in Role**, click **Assign New Built-in Role**, and select a built-in role.
1. To assign a custom role, under **Custom Role**, click **Assign New Custom Role**, and select a custom role.

    By default, users have access to all universes.

1. To customize access to universes, click **Edit Selection**, and select the universes that you want to grant access for. Select the **Include future universes** option to automatically grant access to any universe created in the future. Click **Confirm** when you are done.
1. Click **Add User**.

To modify a user, do the following:

1. Navigate to **Admin > Access Management > Users**, click **Actions** for the user to modify, and choose **Edit Assigned Roles**.
1. To change the built-in role, under **Built-in Role**, change the role. Click **Assign New Built-in Role** to add a role.
1. To change the custom role, under **Custom Role**, change the existing role. Click **Assign New Custom Role** to add a role.

    By default, users have access to all universes.

1. To customize access to universes, click **Edit Selection**, and select the universes that you want to grant access for. Select the **Include future universes** option to automatically grant access to any universe created in the future. Click **Confirm** when you are done.
1. Click **Edit User**.

To delete a user, navigate to **Admin > Access Management > Users**, click **Actions** for the user to delete, and choose **Delete User**.

## Roles

YugabyteDB Anywhere includes built-in roles, and you can define custom roles for team members to restrict access to specific account features.

[API tokens](../../anywhere-automation/) generated for users are assigned the same role as the user that generated them.

To see the users that are assigned a specific role, navigate to **Admin > Access Management > Roles**, click **Actions** for the role, and choose **View Assigned Users**.

### Built-in roles

The following built-in roles are available:

- **Super Admin** is the first user that is created during installation. This role has the highest level of privilege and allows all read and write actions on all YugabyteDB Anywhere resources. There can be only one Super Admin. Super Admin can perform the following:

  - Manage all resources, including universes, nodes, backup, restore, and cloud providers.
  - Manage the user access control by creating and managing users.

  For more information, see [Create admin user](../../configure-yugabyte-platform/create-admin-user/).

- **Admin** has privileges that are similar to the Super Admin, except that Admin cannot manage global scope artifacts and actions, such as runtime configuration settings and LDAP authentication.

- **Backup Admin** has access to backup-related tasks, such as the following:

  - Manage database backups and restore operations.
  - Create new backups.
  - Delete any existing backup or backup schedule.
  - Edit existing backups.

  Backup Admin has View permissions for all other resources.

- **Read-only** access level provides view permissions for the UI and API.

- **Connect-only** access level allows users to sign in and access their user profile only. This role is assigned to users who are not explicitly assigned a role.

You can't delete or edit built-in roles.

### Create, modify, and delete roles

As a Super Admin or Admin, you can:

- create custom roles
- clone built-in and custom roles
- modify and delete custom roles

To create a custom role, do the following:

1. Navigate to **Admin > Access Management > Roles** and click **Create Role**.

    ![Add Role](/images/yp/authorization-platform/add-role.png)

1. Enter a name for the role.
1. Enter a description for the role.
1. Click **Select Permissions**.
1. Select the permissions to assign to the role and click **Confirm** when you are done.
1. Click **Save**.

To create a custom role from an existing role, do the following:

1. Navigate to **Admin > Access Management > Roles**, click **Actions** for the role to clone, and choose **Clone Role**.
1. Enter a name for the role.
1. Enter a description for the role.
1. Click **Edit Permissions**.
1. Select the permissions to assign to the role and click **Confirm** when you are done.
1. Click **Save**.

To edit a custom role, do the following:

1. Navigate to **Admin > Access Management > Roles**, click **Actions** for the role to edit, and choose **Edit Role**.
1. On the **Configurations** tab click **Edit Permissions**.
1. Select the permissions to assign to the role and click **Confirm** when you are done.
1. Click **Save**.

To delete a role, navigate to **Admin > Access Management > Roles**, click **Actions** for the role to edit, and choose **Edit Role**.

To view the users that have been assigned a role, navigate to **Admin > Access Management > Roles**, click **Actions** for the role, and choose **View Assigned Users**.

## Limitations

- Currently, the View Universe permission additionally requires the View Resource permission to work correctly with metrics, performance advisor, and other resources.
- Deleting backups via the delete backup API requires the Delete Resource permission, but when deleting a universe you can choose to delete the associated backups even if you only have the Delete Universe permission.
- Retrying and aborting a task can require different permissions than executing it the first time.
- Currently, when creating a user, you can select **Include Future Universes** only when you have selected all current universes.
- You may need to refresh your browser after creating a universe to apply the permissions for the newly created universe.
- Currently, LDAP group mapping is not supported for custom roles. Only built-in roles are supported for LDAP users.
- The View Resource permission provides view access to all logs, task logs, and so on.
