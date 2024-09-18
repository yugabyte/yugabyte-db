---
title: Manage account access
headertitle: Manage account users
linkTitle: Manage account users
description: Manage access to your YugabyteDB Aeon account and your clusters.
headcontent: Invite team members to your YugabyteDB Aeon account
aliases:
  - /preview/yugabyte-cloud/cloud-admin/manage-access/
menu:
  preview_yugabyte-cloud:
    identifier: manage-cloud-access
    parent: managed-security
    weight: 100
type: docs
---

Invite team members to your account so that they can create and manage clusters, manage billing, audit account activity, and more. Account users are assigned [roles](../managed-roles/), which can be customized to provide access to only the actions and resources needed to perform their tasks.

Users can log in to YugabyteDB Aeon using an email-based account, a social login, or federated authentication via an external identity provider (IdP). For information on using social logins and federated authentication, refer to [Authentication](../managed-authentication/).

(To access a cluster database, you need to ask a user with administrative privileges on the database for the username and password of a [database user created on your behalf](../../cloud-secure-clusters/add-users/).)

The **Users** tab displays a list of users that are either active or have been invited, including their email, display name, role, and status.

![Users page](/images/yb-cloud/managed-admin-users.png)

## Invite users

You add users by sending them an invitation.

To invite a user:

1. Navigate to **Security > Access Control > Users**, then click **Invite User** to display the **Invite User** dialog.
1. Enter one or more email addresses of people you're inviting.
1. Choose a [role](../managed-roles/) for the new users.
1. Click **Invite**.

Users will receive a verification email with a link to create their account and set up a password. Invitations expire after 24 hours.

For users that have not responded to their invitation (their status is Invited), you can click **Resend Invite** to send a fresh invitation.

## Delete a user

You cannot delete your own account.

To delete a user, in the row of the user you want to delete, click the trash icon, then click **Confirm**.

## Change a user's role

You cannot change your own role.

To change a user's role, in the **User Role** column, select a role for the user.

{{< warning title="Use caution when changing roles" >}}

Changing user roles can impact account security. Use caution when assigning roles with permissions for changing and assigning roles, inviting users, or creating API keys. If you are unsure, review role details before assigning.

{{< /warning >}}

To create and manage roles, refer to [Manage account roles](../managed-roles/).

## Reset your password

To reset your password, click **Forgot your password?** on the [login screen](https://cloud.yugabyte.com/login). You'll receive a verification email with instructions to reset your password. You have 60 minutes in which to reset your password, and your old password is no longer valid.

If you don't receive the reset email, click **Resend password reset mail** on the **Reset your password** page. You can always get to this page by clicking **Forgot your password?** on the login page.

If your reset link has expired, request a new password again by clicking **Forgot your password?** on the login page.

You can't change your password, or request a password reset, within 60 minutes of changing the old password.

If you request a password reset for a non-existent user, the request fails silently for security reasons.

## Change your password and display name

To change your password or display name, click the Profile icon in the top right corner and choose **Profile** to display the **User Profile** page.

You can edit your first and last name. Click **Change Password** to change your YugabyteDB Aeon password.
