---
title: YugabyteDB Aeon Federated authentication
headerTitle: Federated authentication
linkTitle: Federated authentication
description: Use federated authentication for single sign-on.
headContent: Single sign-on using an identity provider
menu:
  preview_yugabyte-cloud:
    identifier: federated-custom
    parent: managed-authentication
    weight: 20
type: docs
---

Using federated authentication, you can use an enterprise IdP to manage access to your YugabyteDB Aeon account. After federated authentication is enabled, only Admin users can sign in using email-based login.

Currently, YugabyteDB Aeon supports IdPs exclusively using the OIDC (OpenID Connect) protocol.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../federated-entra/" class="nav-link">
      Microsoft Entra
    </a>
  </li>

  <li>
    <a href="../federated-ping/" class="nav-link">
      PingOne
    </a>
  </li>

  <li>
    <a href="../federated-okta/" class="nav-link">
      Okta
    </a>
  </li>

  <li>
    <a href="../federated-jump/" class="nav-link">
      JumpCloud
    </a>
  </li>

  <li>
    <a href="../federated-custom/" class="nav-link active">
      Custom
    </a>
  </li>

</ul>

## Prerequisites

Before configuring federated authentication, be sure to allow pop-up requests from your IdP. While configuring federated authentication, the provider needs to confirm your identity in a new window.

## Configure federated authentication

To configure federated authentication using a custom IdP in YugabyteDB Aeon, do the following:

1. Navigate to **Security > Access Control > Authentication**, and click **Enable Federated Authentication** to display the **Enable Federated Authentication** dialog.
1. Choose Custom identity provider.

1. Complete the OIDC configuration settings.

    - In the **Client ID** field, enter the unique identifier that you provided when you manually created the client application in the identity provider.
    - In the **Client Secret** field, enter the password or secret for authenticating your Yugabyte client application with your identity provider.
    - In the **Issuer URL** field, provide a URL for the .

    - In the **Authorization endpoint** field, provide .

    - If you have configured OIDC to use [refresh tokens](https://openid.net/specs/openid-connect-core-1_0.html#RefreshTokens), in the **Token endpoint** field, enter the URL of the refresh token endpoint.
    - In the **JKWS endpoint** field, enter .
    - In the **User info endpoint** field, enter .

1. Click **Enable**.

You are redirected to sign in to your IdP to test the connection. After the test connection is successful, federated authentication is enabled.
