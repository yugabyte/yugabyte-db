---
title: Social logins
headertitle: Social logins
linkTitle: Social logins
description: Use social logins to manage authentication.
headcontent: Use social logins to manage authentication
menu:
  preview_yugabyte-cloud:
    identifier: social-logins
    parent: managed-authentication
    weight: 10
type: docs
rightNav:
  hideH4: true
---

The available social logins include Google, GitHub, and LinkedIn. All three are enabled by default.

To manage the social logins available to your account users, do the following:

1. Navigate to **Security > Access Control > Authentication** and click **Edit Configuration** to display the **Login Methods** dialog.
1. Enable the social logins you want to use.
1. Click **Save Changes**.

If you revoke a social login that is already in use, users using that social login can either [reset their password](../manage-access/#reset-your-password) to configure email-based login, or sign in using a different social login. The social account must be associated with the same email address.
