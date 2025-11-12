---
title: YugabyteDB Aeon Federated authentication
headerTitle: Federated authentication
linkTitle: Federated authentication
description: Use federated authentication for single sign-on.
headContent: Single sign-on using an identity provider
menu:
  stable_yugabyte-cloud:
    identifier: federated-5-custom
    parent: managed-authentication
    weight: 20
type: docs
---

Using federated authentication, you can use an enterprise IdP to manage access to your YugabyteDB Aeon account. After federated authentication is enabled, only Admin users can sign in using password authentication.

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

{{<tags/feature/ea>}}Custom federated authentication is Early Access.

## Prerequisites

Before configuring federated authentication, be sure to allow pop-up requests from your IdP; the provider may need to confirm your identity in a new window.

To configure federated authentication in YugabyteDB Aeon, you need the following:

- OIDC environment set up (for example, Google Cloud OIDC).
- Identity provider configured (for example, Google Workspace).
- Permissions to create applications on your OIDC provider.
- An OIDC-based web application created on your OIDC provider, configured with the YugabyteDB Okta redirect URL:

  `https://yugabyte-cloud.okta.com/oauth2/v1/authorize/callback`

  When creating the application, set the allowed scopes to at least openid, profile, and email.

  Optionally, you can restrict access to specific user groups.

  You will need the following for your application:
  - Client ID of the application you registered with your IdP.
  - Client secret of the application.
- The following information from your OIDC provider's discovery document:
  - Issuer URL
  - Authorization endpoint
  - Token endpoint
  - JWKS endpoint
  - User info endpoint

  The discovery document is a JSON file stored in a well-known location.

  [Google OIDC discovery endpoint](https://developers.google.com/identity/protocols/oauth2/openid-connect#an-id-tokens-payload) is an example of such file. For most identity providers, `/.well-known/openid-configuration` is appended to the issuer to generate the metadata URL for OIDC specifications.

For more information on setting up OIDC, consult your IdP provider documentation.

## Configure federated authentication

To configure federated authentication using a custom IdP in YugabyteDB Aeon, do the following:

1. Navigate to **Security > Access Control > Authentication**, and click **Enable Federated Authentication** to display the **Enable Federated Authentication** dialog.
1. Choose Custom identity provider.

1. Complete the OIDC configuration settings.

    - In the **Client ID** field, enter the unique identifier that you provided when you manually created the client application in the identity provider.
    - In the **Client Secret** field, enter the password or secret for authenticating your Yugabyte client application with your identity provider.
    - In the remaining fields, provide the URLs from your provider's discovery document.

1. Click **Enable**.

At this point, you will be redirected to sign in to your IdP to test the connection. If the test connection is successful, federated authentication is enabled.
