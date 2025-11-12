---
title: Social logins
headertitle: Social logins
linkTitle: Social logins
description: Use social logins to manage authentication.
headcontent: Use social logins to manage authentication
menu:
  stable_yugabyte-cloud:
    identifier: social-logins
    parent: managed-authentication
    weight: 10
type: docs
rightNav:
  hideH4: true
---

In addition to password login, you can use social logins, including Google, GitHub, and LinkedIn, to access your account.

To manage the social logins available to your account users, do the following:

1. Navigate to **Security > Access Control > Authentication**, and click **Edit Configuration** to display the **Login Methods** dialog.

    ![Login methods](/images/yb-cloud/managed-authentication-social.png)

1. Enable the login methods you want to use.
1. Click **Save Changes**.

If you revoke a social login that is already in use, users using that social login can either [reset their password](../../manage-access/#reset-your-password) to configure email-based login, or sign in using a different social login. The social account must be associated with the same email address.
