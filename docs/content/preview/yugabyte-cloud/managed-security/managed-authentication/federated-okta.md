---
title: YugabyteDB Aeon Federated authentication
headerTitle: Federated authentication
linkTitle: Federated authentication
description: Use federated authentication for single sign on.
headContent: Use federated authentication for single sign on
menu:
  preview_yugabyte-cloud:
    identifier: federated-okta
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
    <a href="../federated-okta/" class="nav-link active">
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

## Create an application in Okta

Before enabling federated authentication in YugabyteDB Aeon, you must configure your IdP and obtain the necessary credentials.

To use Okta for your IdP, do the following:

1. Sign in to your Okta account and create an app integration.

    - In the Admin Console, go to **Applications > Applications**, and click **Create App Integration**.
    - Set **Sign-in method** to **OIDC**, and **Application type** to **Web Application**, then click **Next**.

    This displays the **New Web App Integration** window.

1. Set the web app integration settings as follows.

    - Enter a name for the app integration.
    - Set **Grant type** to **Authorization Code** (the default).
    - For the **Sign-in redirect URIs**, enter `https://yugabyte-cloud.okta.com/oauth2/v1/authorize/callback` as redirect URI.
    - Delete any **Sign-out redirect URIs** entries, if present.
    - Under **Assignments**, select **Limit access to selected groups** and enter the names of the [user groups](https://help.okta.com/asa/en-us/content/topics/adv_server_access/docs/setup/groups.htm) you want to access YugabyteDB Aeon.
    - Click **Save**.

Your application is added to the **Applications** page.

To configure Okta federated authentication in YugabyteDB Aeon, you need the following application properties:

- Client ID and secret of the application you created. These are provided on the **General** tab.
- Your Okta domain. Click your account name in the top right corner of the Okta Admin Console; the domain is displayed under your account name.

For more information, refer to [App integrations](https://help.okta.com/oie/en-us/content/topics/apps/apps_apps.htm) in the Okta Identity Engine documentation.

## Configure federated authentication

To configure federated authentication in YugabyteDB Aeon, do the following:

1. Navigate to **Security > Access Control > Authentication**, and click **Enable Federated Authentication** to display the **Enable Federated Authentication** dialog.
1. Choose Okta identity provider.
1. Enter the client ID and secret of the Okta application you created.
1. Enter the Okta domain for your application.
1. Click **Enable**.

You are redirected to sign in to your IdP to test the connection. After the test connection is successful, federated authentication is enabled.

## Learn more

- [Enhanced Identity Security with Okta and PingOne Single Sign-On Integrations](https://www.yugabyte.com/blog/single-sign-on-okta-pingone/)