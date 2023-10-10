---
title: Manage roles for YugabyteDB Managed using RBAC
headertitle: Manage account roles
linkTitle: Manage account roles
description: Manage user roles using RBAC in YugabyteDB Managed accounts.
headcontent: Create custom roles for team members
image: /images/section_icons/secure/create-roles.png
aliases:
  - /preview/yugabyte-cloud/cloud-admin/managed-roles/
menu:
  preview_yugabyte-cloud:
    identifier: managed-roles
    parent: managed-security
    weight: 150
type: docs
---

YugabyteDB Managed uses role-based access control (RBAC) to manage access to your YugabyteDB Managed account. Using roles, you can enforce the [principle of least privilege](https://en.wikipedia.org/wiki/Principle_of_least_privilege) (PoLP) by ensuring that users have the precise permissions needed to fulfill their roles while mitigating the risk of unauthorized access or accidental breaches. A role defines a set of permissions that determine what features can be accessed by account users who have been assigned that role.

<div style="position: relative; padding-bottom: calc(48.5% + 44px); height: 0;"><iframe src="https://app.supademo.com/embed/8qhuZOgCCzczVXY_5pydu" frameborder="0" webkitallowfullscreen="true" mozallowfullscreen="true" allowfullscreen style="position: absolute; top: 0; left: 0; width: 100%; height: 100%;"></iframe></div>

YugabyteDB Managed includes [built-in roles](#built-in-roles), and you can [define custom roles](#create-a-role) for team members to restrict access to specific account features. For information on assigning roles to users, refer to [Change a user's role](../manage-access/#change-a-user-s-role).

Roles are also assigned to [API keys](../../managed-automation/managed-apikeys/) to delineate what functionality is available to users accessing your account using either the YugabyteDB Managed API or YBM CLI. You assign roles to API keys when creating the key; refer to [Create an API key](../../managed-automation/managed-apikeys/#create-an-api-key).

{{< tip title="YugabyteDB Managed account users are not the same as database users" >}}

Account users and roles are distinct from the users and roles on your YugabyteDB databases. For information on managing database users, refer to [Add database users](../../cloud-secure-clusters/add-users/).

{{< /tip >}}

The **Roles** tab displays a list of roles that are defined for your account, including the role name, description, type, the number of users assigned the role, and the number of API keys created for the role.

![Roles page](/images/yb-cloud/managed-admin-roles.png)

To view role details, select the role in the list.

## Built-in roles

YugabyteDB Managed includes built-in roles for managing your account:

- **Admin** - The Admin role provides full access to all features. There must always be at least one Admin user. The primary account user (the user who created the YugabyteDB Managed account) is automatically assigned an Admin role.

- **Developer** - The Developer role provides access to all features, with the exception of the following administrative tasks:

  - invite users
  - delete or change the role of other users
  - change login methods
  - create or revoke API keys
  - create a billing profile
  - view account activity

- **Viewer** - The Viewer role has _all_ view permissions, exclusively, and can't perform any tasks.

You can't delete or edit built-in roles.

## Create a role

To create a custom role, do the following:

1. Navigate to **Security > Access Control > Roles**, then click **Create a Role** to display the **Create a Role** dialog.
1. Enter a name for the role.
1. Enter a description for the role.
1. Click **Select Permissions**.
1. Select the permissions to assign to the role and click **Select** when you are done.
1. Click **Save**.

To create a custom role from an existing role, do the following:

1. Navigate to **Security > Access Control > Roles**, then select the role to clone to display the **Role Details** sheet.
1. For a built-in role, click **Clone Role**; for a custom role, click **Actions** and choose **Clone Role**.
1. Enter a name for the role.
1. Enter a description for the role.
1. Click **Edit Permissions**.
1. Select the permissions to assign to the role and click **Select** when you are done.
1. Click **Save**.

## Edit a role

You can only edit custom roles. To edit a custom role, do the following:

1. Navigate to **Security > Access Control > Roles**, then select the custom role to modify to display the **Role Details** sheet.
1. Click **Actions** and **Edit Role**.
1. Edit the name of the role.
1. Edit the description of the role.
1. Click **Edit Permissions**.
1. Select the permissions to assign to the role and click **Select** when you are done.
1. Click **Save**.

## Delete a role

You can only delete custom roles, and only if the role is not assigned to any users.

To delete a custom role, do the following:

1. Navigate to **Security > Access Control > Roles**, then select the custom role to delete to display the **Role Details** sheet.
1. Click **Actions** and **Delete Role**.
1. Enter the role name and click **Delete Role**.
