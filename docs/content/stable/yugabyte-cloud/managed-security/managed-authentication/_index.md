---
title: Manage account authentication
headertitle: Manage account authentication
linkTitle: Authentication
description: Use social logins and identity providers to manage authentication.
headcontent: Use social logins and identity providers to manage authentication
menu:
  stable_yugabyte-cloud:
    parent: managed-security
    identifier: managed-authentication
    weight: 300
type: indexpage
---

In addition to password authentication, you can use social logins or federated authentication via an external identity provider (IdP) to provide access to your YugabyteDB Aeon account.

To configure social logins and federated authentication, navigate to **Security > Access Control > Authentication**.

{{< tip title="Allow pop-ups for federated authentication" >}}

If you have set up federated authentication, let your users know they should allow pop-up requests from your IdP.

{{< /tip >}}

{{<index/block>}}

  {{<index/item
    title="Social logins"
    body="Sign in using Google, GitHub, and LinkedIn."
    href="social-login/"
    icon="/images/section_icons/secure/authorization.png">}}

  {{<index/item
    title="Federated authentication"
    body="Sign in using an identity provider."
    href="federated-entra/"
    icon="/images/section_icons/secure/authorization.png">}}

{{</index/block>}}
