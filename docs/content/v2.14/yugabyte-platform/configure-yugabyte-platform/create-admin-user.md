---
title: Create an admin user
headerTitle: Create an admin user
linkTitle: Create admin user
description: Create an admin user.
menu:
  v2.14_yugabyte-platform:
    identifier: create-admin-user
    parent: configure-yugabyte-platform
    weight: 10
type: docs
---

You can create an admin account via `http://<yugabytedbanywhere-host-ip>/register`, replacing *yugabytedbanywhere-host-ip* with the IP address hosting your YugabyteDB Anywhere instance, as per the following example:

```output
http://10.170.1.192/register
```

The following illustration shows the admin console:

![Register](/images/ee/register.png)

Note that by default, YugabyteDB Anywhere runs as a single-tenant application.

You proceed with the account creation as follows:

- Select the environment.
- Enter your new user credentials.
- Confirm the user agreement.
- Click **Register**.

You are now redirected to the login page located at `http://<yugabydbanywhere-host-ip>/login`. This page allows you to log in to YugabyteDB Anywhere using your new credentials.

Once logged in, you may change the information related to your account by clicking **User > User Profile** at the top right.
