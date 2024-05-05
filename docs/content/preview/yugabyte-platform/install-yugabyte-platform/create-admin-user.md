---
title: Create admin user
headerTitle: Create admin user
linkTitle: Create admin user
description: Create an admin user.
menu:
  preview_yugabyte-platform:
    identifier: create-admin-user
    parent: install-yugabyte-platform
    weight: 20
aliases:
  - /preview/yugabyte-platform/configure-yugabyte-platform/create-admin-user/
type: docs
---

You can access YugabyteDB Anywhere via an Internet browser that has been supported by its maker in the past 24 months and that has a market share of at least 0.2%. In addition, you can access YugabyteDB Anywhere via most mobile browsers, except Opera Mini.

YugabyteDB Anywhere provides the following built-in [roles for user accounts](../../administer-yugabyte-platform/anywhere-rbac/): Super Admin, Admin, Backup Admin, Read only, and Connect only. Unless otherwise specified, the YugabyteDB Anywhere documentation describes the functionality available to a Super Admin user.

The first step after installing YugabyteDB Anywhere is to create your Super Admin account. You can subsequently use this account to create additional users and roles, configure your YugabyteDB Anywhere instance, and manage your user profile.

A YugabyteDB Anywhere installation can have only one Super Admin user.

## Create admin account

You can create an admin account via `https://<yugabytedbanywhere-host-ip>/register`, replacing *yugabytedbanywhere-host-ip* with the IP address hosting your YugabyteDB Anywhere instance, as per the following example:

```output
https://10.170.1.192/register
```

The following illustration shows the admin console:

![Register](/images/ee/register.png)

Note that by default YugabyteDB Anywhere runs as a single-tenant application.

You proceed with the account creation as follows:

- Select the environment.
- Enter your new user credentials.
- Confirm the user agreement.
- Click **Register**.

You are now redirected to the sign in page located at `https://<yugabytedb-anywhere-host-ip>/login`.

Sign in to YugabyteDB Anywhere using your new credentials.

## Change your account information

After signing in, you may change the information related to your account, including the user name and password, by clicking the **User** icon at the top right, and then selecting **User Profile**.
