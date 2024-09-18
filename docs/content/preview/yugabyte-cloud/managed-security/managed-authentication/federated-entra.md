---
title: YugabyteDB Aeon Federated authentication
headerTitle: Federated authentication
linkTitle: Federated authentication
description: Use federated authentication for single sign-on.
headContent: Single sign-on using an identity provider
menu:
  preview_yugabyte-cloud:
    identifier: federated-entra
    parent: managed-authentication
    weight: 20
type: docs
---

Using federated authentication, you can use an enterprise IdP to manage access to your YugabyteDB Aeon account. After federated authentication is enabled, only Admin users can sign in using email-based login.

Currently, YugabyteDB Aeon supports IdPs exclusively using the OIDC (OpenID Connect) protocol.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../federated-entra/" class="nav-link active">
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
</ul>

## Prerequisites

Before configuring federated authentication, be sure to allow pop-up requests from your IdP. While configuring federated authentication, the provider needs to confirm your identity in a new window.

## Register an application with Microsoft identity platform

Before enabling federated authentication in YugabyteDB Aeon, you must configure your IdP and obtain the necessary credentials.

To use Entra for your IdP, you need to register an application with Microsoft Entra so the Microsoft identity platform can provide authentication and authorization services for your application. Configure the application as follows:

- Provide a name for the application.
- Set the sign-in audience for the application to **Accounts in any organizational directory** (Multitenant).

    ![Azure account types](/images/yb-cloud/managed-authentication-azure-account-types.png)

- Set the Redirect URI platform to Web, and the URI to the following:

    ```sh
    https://yugabyte-cloud.okta.com/oauth2/v1/authorize/callback
    ```

Use your own Entra account to test the connection. For more information, refer to [Register an application with the Microsoft identity platform](https://learn.microsoft.com/en-us/entra/identity-platform/quickstart-register-app) in the Microsoft documentation.

In addition, to configure Entra federated authentication in YugabyteDB Aeon, you need the following:

- Client ID of the application you registered.
- Client secret of the application.

Refer to [Create a new client secret](https://learn.microsoft.com/en-us/entra/identity-platform/howto-create-service-principal-portal#option-3-create-a-new-client-secret) in the Microsoft documentation.

## Configure federated authentication

To configure federated authentication in YugabyteDB Aeon, do the following:

1. Navigate to **Security > Access Control > Authentication** and click **Enable Federated Authentication** to display the **Enable Federated Authentication** dialog.
1. Choose the Microsoft Entra ID identity provider.
1. Enter your Entra application client ID and secret.
1. Click **Enable**.

You are redirected to sign in to your IdP to test the connection. Once test connection is successful, federated authentication is enabled.
