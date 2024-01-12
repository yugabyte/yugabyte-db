---
title: Manage account authentication
headertitle: Manage account authentication
linkTitle: Authentication
description: Use social logins and identity providers to manage authentication.
headcontent: Use social logins and identity providers to manage authentication
menu:
  preview_yugabyte-cloud:
    identifier: managed-authentication
    parent: managed-security
    weight: 300
type: docs
rightNav:
  hideH4: true
---

In addition to email-based accounts, you can use social logins or federated authentication via an external identity provider (IdP) to provide access to your YugabyteDB Managed account.

The **Authentication** tab displays options for configuring social logins and federated authentication.

![Authentication page](/images/yb-cloud/managed-authentication.png)

## Social logins

The available social logins include Google, GitHub, and LinkedIn. All three are enabled by default.

To manage the social logins available to your account users, do the following:

1. Navigate to **Security > Access Control > Authentication**, then click **Edit Configuration** to display the **Login Methods** dialog.
1. Enable the social logins you want to use.
1. Click **Save Changes**.

If you revoke a social login that is already in use, users using that social login can either [reset their password](../manage-access/#reset-your-password) to configure email-based login, or sign in using a different social login. The social account must be associated with the same email address.

## Federated authentication

Using federated authentication, you can use an IdP to manage access to your YugabyteDB Managed account.

Note that after federated authentication is enabled, only Admin users can sign in using email-based login.

Currently, YugabyteDB Managed supports the Microsoft Entra ID and PingOne for Enterprise IdPs, and the OIDC protocol.

### Prerequisites

Before configuring federated authentication, keep in mind the following:

- Be sure to allow pop-up requests from your IdP. While configuring federated authentication, the provider needs to confirm your identity in a new window.

{{< tabpane text=true >}}

  {{% tab header="Microsoft Entra" lang="entra" %}}

**Register an application**

To use Entra for your IdP, you need to register an application with Microsoft Entra so the Microsoft identity platform can provide authentication and authorization services for your application. Configure the application as follows:

- Provide a name for the application.
- Set the sign-in audience for the application to **Accounts in any organizational directory** (Multitenant).

    ![Azure account types](/images/yb-cloud/managed-authentication-azure-account-types.png)

- Set the Redirect URI platform to Web, and the URI to `https://yugabyte-cloud.okta.com/oauth2/v1/authorize/callback`.

Use your own Entra account to test the connection. For more information, refer to [Register an application with the Microsoft identity platform](https://learn.microsoft.com/en-us/entra/identity-platform/quickstart-register-app) in the Microsoft documentation.

In addition, to configure Entra federated authentication in YugabyteDB Managed, you need the following:

- Client ID of the application you registered.
- Client secret of the application.

Refer to [Create a new client secret](https://learn.microsoft.com/en-us/entra/identity-platform/howto-create-service-principal-portal#option-3-create-a-new-client-secret) in the Microsoft documentation.

**Configure**

To configure federated authentication using Entra, do the following:

1. Navigate to **Security > Access Control > Authentication**, then click **Enable Federated Authentication** to display the **Enable Federated Authentication** dialog.
1. Choose the Microsoft Entra ID identity provider.
1. Enter your Entra application client ID and secret.
1. Click **Enable**.

You are redirected to sign in to your IdP to test the connection. Once test connection is successful, federated authentication is enabled.

  {{% /tab %}}

  {{% tab header="PingOne" lang="ping" %}}

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

1. On the **Access** tab, click **Edit**, select the user groups you want to access YugabyteDB Managed, and click **Save** when you are done.

1. Enable the application by turning on the slider control at the top of the page.

To configure PingOne federated authentication in YugabyteDB Managed, you need the following application properties:

- Client ID and secret of the application you created. These are provided on the **Overview** and **Configuration** tabs.
- Authorization URL for your application. This is displayed on the **Configuration** tab under **URLs**.

For more information, refer to the [PingOne for Enterprise](https://docs.pingidentity.com/r/en-us/pingoneforenterprise/p14e_landing) documentation.

**Configure**

To configure federated authentication using PingOne, do the following:

1. Navigate to **Security > Access Control > Authentication**, then click **Enable Federated Authentication** to display the **Enable Federated Authentication** dialog.
1. Choose PingOne identity provider.
1. Enter the client ID and secret of the PingOne application you created.
1. Enter the Authorization URL for your application.
1. Click **Enable**.

You are redirected to sign in to your IdP to test the connection. Once test connection is successful, federated authentication is enabled.

  {{% /tab %}}

{{< /tabpane >}}
