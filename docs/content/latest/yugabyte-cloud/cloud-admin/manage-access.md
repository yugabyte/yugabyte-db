---
title: Manage cloud access
linkTitle: Users
description: Manage access to Yugabyte Cloud and your clusters.
headcontent:
image: /images/section_icons/secure/create-roles.png
menu:
  latest:
    identifier: manage-cloud-access
    parent: cloud-admin
    weight: 100
isTocNested: true
showAsideToc: true
---

Invite other users to your cloud so that they can create, manage, and connect to clusters, and add IP allow lists. Invited users cannot perform cloud administration tasks.

Only the cloud account Admin user (the user who created the Yugabyte Cloud account) can invite users. 

The **Users** tab displays a list of users that have been invited to the cloud, including their email, display name, role, and status.

![Admin Users page](/images/yb-cloud/cloud-admin-users.png)

## User roles

A Yugabyte Cloud user is either a Developer or the console Admin.

### Admin

The Admin user is the primary account user (the user who created the Yugabyte Cloud account), and has full access to all cloud console features. There is only one Admin account, and this account cannot be deleted or transferred.

### Developer

Anyone who has been invited to the cloud by the Admin user is assigned the Developer role. Developer users have access to cluster management, but cannot perform any administrative tasks, including the following:

- invite users to the cloud.
- delete other users.
- create a billing profile. 

To access a cluster database, you need to ask the Admin for the username and password of a database user created on your behalf.

## Invite users

You add users to your cloud by sending them an invitation. Only the Admin user can invite users.

To invite a user:

1. On the **Users** tab, click **Invite User** to display the **Invite User** dialog.
1. Enter the email address of the person you're inviting.
1. Click **Invite**.

The user will receive a verification email with a link to create their account and set up a password.

For users that have not responded to their invitation (their status is Invited), you can click **Resend Invite**.

## Delete a user

Only the Admin user can delete users. The Admin account cannot be deleted.

To delete a user, click **Delete** next to the username in the list, then click **Confirm**.

<!--
## View user details

The user details include a note that the credentials give admin access to the Yugabyte database, the username, and the password. 
Click **INFO** under **Actions** for the user whose details you want to see.
To see the password, click **Show password**. Click **Close** when finished.
-->
