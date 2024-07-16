---
title: YugabyteDB Aeon Federated authentication
headerTitle: Federated authentication
linkTitle: Federated authentication
description: Use federated authentication for single sign on.
headContent: Use federated authentication for single sign on
menu:
  preview_yugabyte-cloud:
    identifier: federated-ping
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
    <a href="../federated-ping/" class="nav-link active">
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

## Create an application in PingOne

Before enabling federated authentication in YugabyteDB Aeon, you must configure your IdP and obtain the necessary credentials.

To use PingOne for your IdP, do the following:

1. Sign in to your PingIdentity account and create an application.

    - Under **Applications**, add a new application.
    - Enter a name for the application.
    - Under **Application Type**, select **OIDC Web App**.
    - Click **Save**.

1. Select the application you created and, on the **Configuration** tab, click **Edit** and set the following options:

    - **Response Type** - select **Code**.
    - **Grant Type** - select **Authorization Code**.
    - **Redirect URIs** - enter `https://yugabyte-cloud.okta.com/oauth2/v1/authorize/callback`.
    - **Token Endpoint Authentication Method** - select **Client Secret Post**.
    - **Initiate Login URI** - enter `https://cloud.yugabyte.com/login`.

    Click **Save** when you are done.

1. On the **Resources** tab, edit the **ALLOWED SCOPES**, select the **openid**, **email**, and **profile** scopes, and click **Save** when you are done.

1. Configure **Policies** and **Attribute Mappings** as required.

1. On the **Access** tab, click **Edit**, select the user groups you want to access YugabyteDB Aeon, and click **Save** when you are done.

1. Enable the application by turning on the slider control at the top of the page.

To configure PingOne federated authentication in YugabyteDB Aeon, you need the following application properties:

- Client ID and secret of the application you created. These are provided on the **Overview** and **Configuration** tabs.
- Authorization URL for your application. This is displayed on the **Configuration** tab under **URLs**.

For more information, refer to the [PingOne for Enterprise](https://docs.pingidentity.com/r/en-us/pingoneforenterprise/p14e_landing) documentation.

## Configure federated authentication

To configure federated authentication in YugabyteDB Aeon, do the following:

1. Navigate to **Security > Access Control > Authentication** and click **Enable Federated Authentication** to display the **Enable Federated Authentication** dialog.
1. Choose PingOne identity provider.
1. Enter the client ID and secret of the PingOne application you created.
1. Enter the Authorization URL for your application.
1. Click **Enable**.

You are redirected to sign in to your IdP to test the connection. After the test connection is successful, federated authentication is enabled.

## Learn more

- [Enhanced Identity Security with Okta and PingOne Single Sign-On Integrations](https://www.yugabyte.com/blog/single-sign-on-okta-pingone/)