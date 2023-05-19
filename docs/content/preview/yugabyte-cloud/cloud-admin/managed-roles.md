---
title: Manage roles for YugabyteDB Managed
headertitle: Manage account roles
linkTitle: Manage account roles
description: Manage roles that can be assigned to users of your YugabyteDB Managed account.
headcontent: Create custom roles for team members
image: /images/section_icons/secure/create-roles.png
menu:
  preview_yugabyte-cloud:
    identifier: managed-roles
    parent: cloud-admin
    weight: 150
type: docs
---

Access to the features of your YugabyteDB Managed account is managed using roles. A role defines a set of permissions that determine what features can be accessed by account users who have been assigned that role.

Roles are also assigned to [API keys](../../managed-automation/managed-apikeys/) to delineate what functionality is available to users accessing your account using either the YugabyteDB Managed API or YBM CLI.

YugabyteDB Managed includes two built-in roles for managing your account:

- **Admin** - The Admin role provides full access to all features. There must always be at least one Admin user. The primary account user (the user who created the YugabyteDB Managed account) is automatically assigned an Admin role.

- **Developer** - The Developer role provides access to all features, with the exception of the following administrative tasks:

  - invite users
  - delete or change the role of other users
  - change login methods
  - create or revoke API keys
  - create a billing profile
  - view account activity

You can also define custom roles for team members to restrict access to specific account features.

You must be signed in as a user with Role Management permissions to create or modify roles.

The **Roles** tab displays a list of roles that are defined for your account, including the role name, description, type, the number of users assigned the role, and the number of API keys created for the role.

![Admin Roles page](/images/yb-cloud/managed-admin-roles.png)

To view role details, select the role in the list.

## Create a role

To create a role, do the following:

1. Navigate to **Admin > Access Control > Roles**, then click **Create a Role** to display the **Create a Role** dialog.
1. Enter a name for the role.
1. Enter a description for the role.
1. Click **Select Permissions**.
1. Select the permissions to assign to the role and click **Select** when you are done.
1. Click **Save Changes**.

To create a role from an existing role, do the following:

1. Navigate to **Admin > Access Control > Roles**, then select the role to modify to display the **Role Details** sheet.
1. Click **Actions** and **Clone Role**.
1. Enter a name for the role.
1. Enter a description for the role.
1. Click **Edit Permissions**.
1. Select the permissions to assign to the role and click **Select** when you are done.
1. Click **Save**.

## Edit a role

To edit a role, do the following:

1. Navigate to **Admin > Access Control > Roles**, then select the role to modify to display the **Role Details** sheet.
1. Click **Actions** and **Edit Role**.
1. Edit the name of the role.
1. Edit the description of the role.
1. Click **Edit Permissions**.
1. Select the permissions to assign to the role and click **Select** when you are done.
1. Click **Save**.

## Delete a role

You can only delete roles that are not assigned to any users.

To delete a role, do the following:

1. Navigate to **Admin > Access Control > Roles**, then select the role to modify to display the **Role Details** sheet.
1. Click **Actions** and **Delete Role**.
1. Enter the role name and click **Delete Role**.
