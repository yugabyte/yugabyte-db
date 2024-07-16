---
title: YugabyteDB Aeon Federated authentication
headerTitle: Federated authentication
linkTitle: Federated authentication
description: Use federated authentication for single sign on.
headContent: Use federated authentication for single sign on
menu:
  preview_yugabyte-cloud:
    identifier: federated-jump
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
    <a href="../federated-jump/" class="nav-link active">
      JumpCloud
    </a>
  </li>
</ul>

## Prerequisites

Before configuring federated authentication, be sure to allow pop-up requests from your IdP. While configuring federated authentication, the provider needs to confirm your identity in a new window.

## Create an application in JumpCloud

Before enabling federated authentication in YugabyteDB Aeon, you must configure your IdP and obtain the necessary credentials.

To use JumpCloud for your IdP, do the following:

1. Sign in to JumpCloud using an administrator account.

1. Create an application.

    - Under **SSO Applications**, click **Add New Application**.
    - Select **Custom Application**, and make sure the integration supports "SSO with OIDC" on the next page.
    - Under **Manage Single Sign-On (SSO)**, select **Configure SSO with OIDC**, and click **Next**.
    - Under **Enter General Info**, add the application name (for **Display Label**), **Description**, and logo (for **User Portal Image**), and select **Show this application in User Portal**.

      This information is displayed as a tile when users sign in to YugabyteDB Aeon.

    - Click **Configure Application**.

1. Configure your application.

    Under **SSO > Endpoint Configuration**, configure the following:

    - **Redirect URIs** - enter `https://yugabyte-cloud.okta.com/oauth2/v1/authorize/callback`.
    - **Client Authentication Type** - select **Client Secret Post**.
    - **Login URL** - enter `https://cloud.yugabyte.com/login`.

    Under **Attribute Mapping**, for **Standard Scopes**, select **Email** and **Profile**.

    Click **Activate** when you are done.

    You will be prompted in a pop up to save the **Client ID** and **Client Secret**. Save these in a secure location, you will need to provide these credentials in YugabyteDB Aeon.

1. Configure Attributes and Identity Management as required.

1. Integrate the user in JumpCloud.

    - Navigate to **User Groups**, select the user groups you want to access YugabyteDB Aeon, and click **Save** when you are done.

To configure JumpCloud federated authentication in YugabyteDB Aeon, you need the following application properties:

- **Client ID** and **Client Secret** of the application you created. These are the credentials you saved when you activated your application. The **Client ID** is also displayed on the **SSO** tab.

For more information, refer to the [JumpCloud](https://jumpcloud.com/support/sso-with-oidc) documentation.

## Configure federated authentication

To configure federated authentication in YugabyteDB Aeon, do the following:

1. Navigate to **Security > Access Control > Authentication** and click **Enable Federated Authentication** to display the **Enable Federated Authentication** dialog.
1. Choose JumpCloud identity provider.
1. Enter the client ID and secret of the JumpCloud application you created.
1. Click **Enable**.

You are redirected to sign in to your IdP to test the connection. After the test connection is successful, federated authentication is enabled.
