---
title: OIDC authentication using JumpCloud in YugabyteDB Anywhere
headerTitle: OIDC authentication with JumpCloud
linkTitle: OIDC authentication
description: Configuring YugabyteDB Anywhere universe to use OIDC with JumpCloud.
headcontent: Use JumpCloud to authenticate accounts for database access
badges: ea
menu:
  preview_yugabyte-platform:
    identifier: oidc-authentication-jumpcloud
    parent: authentication
    weight: 20
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../oidc-authentication-aad/" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      Azure AD
    </a>
  </li>
  <li >
    <a href="../oidc-authentication-jumpcloud/" class="nav-link active">
      <i class="fa-solid fa-cubes"></i>
      JumpCloud
    </a>
  </li>
</ul>

This section describes how to configure a YugabyteDB Anywhere (YBA) universe to use OIDC-based authentication for YugabyteDB YSQL database access using JumpCloud as the Identity Provider (IdP).

After OIDC is set up, users can sign in to the YugabyteDB universe database using their JSON Web Token (JWT) as their password.

Note that the yugabyte privileged user will continue to exist as a local database user even after OIDC-based authentication is enabled for a universe.

**Learn more**

- [Enable YugabyteDB Anywhere authentication via OIDC](../../../administer-yugabyte-platform/oidc-authentication/)
- [YFTT: OIDC Authentication in YSQL](https://www.youtube.com/watch?v=KJ0XV6OnAnU&list=PL8Z3vt4qJTkLTIqB9eTLuqOdpzghX8H40&index=1)

## Create an application in JumpCloud

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

    - **Redirect URIs** - enter `https://<your YugabyteDB Anywhere IP address>/api/v1/callback?client_name=OidcClient`.
    - **Client Authentication Type** - select **Client Secret Post**.
    - **Login URL** - enter `https://<YugabyteDB Anywhere IP address>/login`.

    Under **Attribute Mapping**, for **Standard Scopes**, select **Email** and **Profile**.

    Click **Activate** when you are done.

    You will be prompted in a pop up to save the **Client ID** and **Client Secret**. Save these in a secure location, you will need to provide these credentials in YugabyteDB Anywhere.

1. Configure Attributes and Identity Management as required.

1. Integrate the user in JumpCloud.

    - Navigate to **User Groups**, select the user groups you want to access YugabyteDB Aeon, and click **Save** when you are done.

To configure JumpCloud federated authentication in YugabyteDB Aeon, you need the following application properties:

- **Client ID** and **Client Secret** of the application you created. These are the credentials you saved when you activated your application. The **Client ID** is also displayed on the **SSO** tab.

For more information, refer to the [JumpCloud](https://jumpcloud.com/support/sso-with-oidc) documentation.

## Configure authentication

To configure User authentication in YugabyteDB Anywhere, do the following:

1. Navigate to **Admin > User Management > User Authentication** and select **ODIC configuration**.
1. Under **OIDC configuration**,  configure the following:

    - **Client ID** and **Client Secret** - enter the client ID and secret of the JumpCloud application you created.
    - **Discovery URL** - enter `https://oauth.id.jumpcloud.com/.well-known/openid-configuration`.
    - **Scope** - enter `openid email`.
    - **Email attribute** - enter your registered email.

1. Click **Save**.

You are redirected to sign in to your IdP to test the connection. After the test connection is successful, federated authentication is enabled.
