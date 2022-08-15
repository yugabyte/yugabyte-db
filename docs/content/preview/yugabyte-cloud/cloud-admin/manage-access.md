---
title: Manage account access
linkTitle: Manage account access
description: Manage access to your YugabyteDB Managed account and your clusters.
headcontent: Invite team members to your YugabyteDB Managed account
image: /images/section_icons/secure/create-roles.png
menu:
  preview_yugabyte-cloud:
    identifier: manage-cloud-access
    parent: cloud-admin
    weight: 100
type: docs
---

Invite team members to your account so that they can create, manage, and connect to clusters. You can also manage the login methods available to users for signing in to your YugabyteDB Managed account.

You must be signed in as an Admin user to invite other users, change roles, and manage login methods.

The **Users** tab displays a list of users that are either active or have been invited, including their email, display name, role, and status.

![Admin Users page](/images/yb-cloud/cloud-admin-users.png)

## User roles

A YugabyteDB Managed user is either an Admin or a Developer.

### Admin

Admin users have full access to all features. There must always be at least one Admin user. The primary account user (the user who created the YugabyteDB Managed account) is automatically assigned an Admin role.

### Developer

Developer users have access to all features, with the exception of administrative tasks, including the following:

- invite users
- delete or change the role of other users
- change login methods
- create a billing profile
- view account activity

To access a cluster database, you need to ask an Admin for the username and password of a [database user created on your behalf](../../cloud-secure-clusters/add-users/).

## Manage login methods

Users can log in to YugabyteDB Managed using either an email-based account or a social login. The available social logins include Google, GitHub, and LinkedIn. All three are enabled by default. Only an Admin user can modify the login methods.

To manage the social logins available to users, do the following:

1. On the **Admin** page, select the **Users** tab, then click **Login Methods** to display the **Login Methods** dialog.
1. Enable the social logins you want to use.
1. Click **Save Changes**.

If you revoke a social login that is already in use, users using that social login can either [reset their password](#reset-your-password) to configure email-based login, or sign in using a different social login. The social account must be associated with the same email address.

## Invite users

You add users by sending them an invitation. Only an Admin user can invite users.

To invite a user:

1. On the **Admin** page, select the **Users** tab, then click **Invite User** to display the **Invite User** dialog.
1. Enter one or more email addresses of people you're inviting.
1. Choose a role for the new users.
1. Click **Invite**.

Users will receive a verification email with a link to create their account and set up a password. Invitations expire after 24 hours.

For users that have not responded to their invitation (their status is Invited), you can click **Resend Invite** to send a fresh invitation.

## Delete a user

Only an Admin user can delete users. You cannot delete your own account.

To delete a user, click **Delete** next to the username in the list, then click **Confirm**.

## Change a user's role

Only an Admin user can change the role of other users. You cannot change your own role.

To change a user's role, in the **User Role** column, select a role for the user.

## Reset your password

To reset your password, click **Forgot your password?** on the [login screen](https://cloud.yugabyte.com/login). You'll receive a verification email with instructions to reset your password. You have 60 minutes in which to reset your password, and your old password is no longer valid.

If you don't receive the reset email, click **Resend password reset mail** on the **Reset your password** page. You can always get to this page by clicking **Forgot your password?** on the login page.

If your reset link has expired, request a new password again by clicking **Forgot your password?** on the login page.

You can't change your password, or request a password reset, within 60 minutes of changing the old password.

If you request a password reset for a non-existent user, the request fails silently for security reasons.

## Change your password and display name

To change your password or display name, click the Profile icon in the top right corner and choose **Profile** to display the **User Profile** page.

You can edit your first and last name. Click **Change Password** to change your YugabyteDB Managed password.
