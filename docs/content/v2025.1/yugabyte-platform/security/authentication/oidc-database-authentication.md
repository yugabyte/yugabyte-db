---
title: OIDC authentication in YugabyteDB Anywhere
headerTitle: OIDC database authentication
linkTitle: OIDC authentication
description: Configuring YugabyteDB Anywhere universe to use OIDC
headcontent: Use OIDC to authenticate accounts for database access
menu:
  v2025.1_yugabyte-platform:
    identifier: oidc-database-authentication
    parent: authentication
    weight: 20
aliases:
  - /v2025.1/yugabyte-platform/security/authentication/oidc-authentication-aad/
  - /v2025.1/yugabyte-platform/security/authentication/oidc-authentication-jumpcloud/
type: docs
rightNav:
  hideH4: true
---

This section describes how to configure a YugabyteDB Anywhere universe to use OIDC-based authentication for YugabyteDB YSQL database access. The integration works with any OIDC-compliant provider.

In normal operation, after OIDC is configured in YugabyteDB Anywhere, database users authenticate with an OIDC provider, which issues a JSON Web Token (JWT). The JWT is a short-lived, signed token that proves the user's identity. Database users must then obtain the JWT (in one of two ways) and use it to log in to the database. One way is to obtain the JWT directly from YugabyteDB Anywhere; this requires the provider to be configured to send the JWT, and YugabyteDB Anywhere to be configured to display it to users on their login screen. Alternatively, users can use a custom tool to obtain the JWT directly from the OIDC provider. In either case, database users then supply this JWT as their database password when connecting directly to YugabyteDB.

Note that the yugabyte privileged user will continue to exist as a local database user even after OIDC-based authentication is enabled for a universe.

To set up OIDC authentication, complete the following steps:

1. [Configure your Identity provider](#configure-your-identity-provider): register YugabyteDB Anywhere as a client application and configure the token claims.
1. [Configure OIDC in YugabyteDB Anywhere (Optional)](#configure-oidc-in-yugabytedb-anywhere-optional): if you want YugabyteDB Anywhere to surface the JWT on the sign-in page, enable OIDC in YugabyteDB Anywhere. Otherwise, you can fetch the JWT directly from the OIDC provider using a tool of your choice.
1. [Configure a universe to use OIDC](#configure-a-universe-to-use-oidc): set the authentication flags on your YugabyteDB universe for YSQL access.
1. [Manage users and roles](#manage-users-and-roles): create database roles that map to the claims in the JWT.

## Configure your Identity provider

Configuring your OIDC provider to work with YugabyteDB typically requires:

- Registering YugabyteDB Anywhere in the OIDC provider.
- Configuring the provider to send (redirect) tokens with the required claims to YugabyteDB Anywhere.

Consult your OIDC provider documentation.

Before you start, note the [YugabyteDB Anywhere callback URI](../../../administer-yugabyte-platform/oidc-authentication/#oidc-callback-uri); you'll need to provide it as the redirect URI when registering your application in your OIDC provider.

The following sections provide instructions for Azure AD (also known as [Microsoft Entra ID](https://www.microsoft.com/en-ca/security/business/identity-access/microsoft-entra-id)) and JumpCloud.

{{< tabpane text=true >}}

  {{% tab header="Azure AD" lang="azure" %}}

### Register an application in Azure

You register your YugabyteDB Anywhere instance as an application in Azure. You will use the application's tenant ID, client ID, and client secret to configure OIDC in YugabyteDB Anywhere.

To register an application, do the following:

1. In the Azure console, navigate to **App registrations** and click **New registration**.

1. Enter a name for the application.

1. Select the tenant for the application.

1. Set the [redirect URI](../../../administer-yugabyte-platform/oidc-authentication/#oidc-callback-uri). This is where the IdP redirects after authentication.

1. Click **Register**.

    After the application is registered, you can obtain the tenant ID and client ID.

1. Click **Add a certificate or secret**.

1. Select **Client secrets** and click **New client secret**.

1. Enter a description and set the expiry for the secret, and then click **Add**.

1. Copy the secret value and keep it in a secure location.

For more information, refer to [Register an application with the Microsoft identity platform](https://learn.microsoft.com/en-us/entra/identity-platform/quickstart-register-app) in the Azure documentation.

#### Configure group claims and roles in Azure AD

By default, the Subject claim is used as the value to determine the role to assign to users for database access. In addition to the standard claims for token expiration, subject, and issuer, you have the option to use a non-standard claim (other than Subject) to determine role assignment. That is, the values of this claim will map the user to the database roles. This claim is denoted as `jwt_matching_claim_key`.

YugabyteDB expects the token created by the IdP to contain the following standard claims as well as the optional `jwt_matching_claim_key` to identify the end user and grant them the right access inside databases:

- Issuer
- Subject
- `jwt_matching_claim_key`, claim key, and values - these could be groups, roles, email, and so forth. Optionally, the subject claim can be used as the claim key.

The claims included in the token and chosen for user authorization will vary depending on your requirements.

For example, to use group memberships as the determining factor for access and role assignment, you would include the groups claim in the initial token sent to the database. Note that the Subject claim can also be used to map the user to the PostgreSQL role.

These token claims are configured in the Azure AD application registration.

The following illustration shows an example of Azure token configuration to ensure the right groups or roles claims are included in the token. Note that these options are available in higher Azure AD tiers.

![Configuring the groups claims in Azure AD Application registrations](/images/yp/security/oidc-azure-editgroup.png)

The following is an example of a decoded JWT with groups claims (the Group GUID is used in Azure AD OIDC tokens):

```json{.nocopy}
"awd": "e12c03b1-7463-8e23-94d2-8d71f17ab99b",
"iss": "https://login.microsoftonline.com/733dee2h-cb2e-41ab-91f2-29e2af034ffe/v2.0",
"iat": 1669657223, "nbf": 1669657223, "exp": 1669651124,
"groups": [
  "a12d04b1-7463-8e23-94d2-8d71f17ab99b",
  "c22b03b1-2746-8d34-54b1-6d32f17ba36b",
],
"oid": "b12a45b2-7463-5e76-32d3-9d31f15ab77b",
"sub": "C12a45bGc463_ADLN632d39d31f15ab77b",
"tid": "a42a45c3-4728-4f98-25d3-6d63f15ab36c",
"ver": "2.0",
"wids" [
  "b12d04b1-7463-8e23-94d2-8d71f17ab99b",
  "f22a03c1-2746-8d34-54b1-6d32f17ba36b",
]
```

Note that GUIDs are not supported for YSQL usernames. Use regex rules in user name maps in the `yb_ident.conf` file to convert group GUIDs to roles. See [ysql_ident_conf_csv](#ysql-ident-conf-csv).

The following illustration shows an example of Azure app roles configuration.

![Configuring App Roles in Azure AD Application registrations](/images/yp/security/oidc-azure-approles.png)

The following shows an example of a decoded JWT with app roles claims:

```json{.nocopy}
"awd": "e12c03b1-7463-8e23-94d2-8d71f17ab99b",
"iss": "https://login.microsoftonline.com/733dee2h-cb2e-41ab-91f2-29e2af034ffe/v2.0",
"iat": 1669657223, "nbf": 1669657223, "exp": 1669651124,
"name": "Fred Summers",
"preferred_username": "fsummers@example.com",
"oid": "b12a45b2-7463-5e76-32d3-9d31f15ab77b",
"roles": [
  "a12d04b1-7463-8e23-94d2-8d71f17ab99b",
  "c22b03b1-2746-8d34-54b1-6d32f17ba36b",
],
"sub": "C12a45bGc463_ADLN632d39d31f15ab77b",
"tid": "a42a45c3-4728-4f98-25d3-6d63f15ab36c",
"ver": "2.0",
"wids" [
  "b12d04b1-7463-8e23-94d2-8d71f17ab99b",
  "f22a03c1-2746-8d34-54b1-6d32f17ba36b",
]
```

For more information on configuring group claims and app roles, refer to [Configuring group claims and app roles in tokens](https://learn.microsoft.com/en-us/security/zero-trust/develop/configure-tokens-group-claims-app-roles) in the Azure documentation.

  {{% /tab %}}

  {{% tab header="JumpCloud" lang="jumpcloud" %}}

### Configure an application in JumpCloud

To use JumpCloud for your IdP, do the following:

1. Sign in to JumpCloud using an administrator account.

1. Create an application.

    - Under **SSO Applications**, click **Add New Application**.
    - Select **Custom Application**, and make sure the integration supports "SSO with OIDC" on the next page.
    - Under **Manage Single Sign-On (SSO)**, select **Configure SSO with OIDC**, and click **Next**.
    - Under **Enter General Info**, add the application name (for **Display Label**), **Description**, and logo (for **User Portal Image**), and select **Show this application in User Portal**.
    - Click **Configure Application**.

1. Configure your application.

    Under **SSO > Endpoint Configuration**, configure the following:

    - **Redirect URIs**. Enter the [OIDC callback URI](../../../administer-yugabyte-platform/oidc-authentication/#oidc-callback-uri). This is where the IdP redirects after authentication.
    - **Client Authentication Type**. Select **Client Secret Post**.
    - **Login URL**. Enter `https://<your-YugabyteDB-Anywhere-IP-address>/login`.

    Under **Attribute Mapping**, for **Standard Scopes**, select **Email** and **Profile**.

    Click **Activate** when you are done.

    You will be prompted in a pop up to save the **Client ID** and **Client Secret**. Save these in a secure location, you will need to provide these credentials in YugabyteDB Anywhere.

1. Configure Attributes and Identity Management as required.

1. Integrate the user in JumpCloud.

    Navigate to **User Groups**, select the user groups you want to access YugabyteDB Anywhere, and click **Save** when you are done.

To [configure](#configure-oidc-in-yugabytedb-anywhere-optional) JumpCloud federated authentication in YugabyteDB Anywhere, you need the following application properties:

- **Client ID** and **Client Secret** of the application you created. These are the credentials you saved when you activated your application. The **Client ID** is also displayed on the **SSO** tab.

For more information, refer to the [JumpCloud](https://jumpcloud.com/support/sso-with-oidc) documentation.

  {{% /tab %}}

{{< /tabpane >}}

## Configure OIDC in YugabyteDB Anywhere (Optional)

You have two options to obtain your JWT from the IdP to connect to the database.

### Use the tool of your choice

You can fetch the JWT directly from the OIDC provider using any OAuth2-capable tool such as the Azure CLI, JumpCloud CLI, `curl`, or Postman, and supply it as the password when connecting to the database. No additional YugabyteDB Anywhere configuration is required for this path.

### Via YugabyteDB Anywhere

YugabyteDB Anywhere can display your JWT on the sign-in page after you authenticate with your OIDC provider.

You need to be signed in as a Super Admin and have your provider credentials:

- Azure: Application client ID, client secret, and tenant ID
- JumpCloud: Application client ID and client secret

To enable OIDC authentication in YugabyteDB Anywhere, do the following:

1. Navigate to **Admin > Access Management > User Authentication** and select **OIDC configuration**.
1. Under **OIDC configuration**,  configure the following:

    - **Client ID** and **Client Secret** - enter the client ID and secret of the application you created.
    - **Discovery URL** - the discovery URL for your provider.
    
        For Azure:

        ```text
        login.microsoftonline.com/<tenant_id>/v2.0/.well-known/openid-configuration
        ```

        For JumpCloud:

        ```text
        https://oauth.id.jumpcloud.com/.well-known/openid-configuration
        ```

    - **Scope** - enter `openid email profile`.
    
        If you are using the Refresh Token feature to allow the OIDC provider server to return the refresh token (which can be used by YugabyteDB Anywhere to refresh the login), enter `openid offline_access profile email` instead.

    - **Email attribute** - enter the email attribute to a name for the property to be used in the mapping file, such as `preferred_username`.
    - **Refresh Token URL** - if you have configured OIDC to use [refresh tokens](https://openid.net/specs/openid-connect-core-1_0.html#RefreshTokens), in the **Refresh Token URL** field, enter the URL of the refresh token endpoint.
    - **Display JWT token on login** - select this option to allow users to access their JWT from the YugabyteDB Anywhere sign in page. This allows a user to view and copy their JWT without signing in to YugabyteDB Anywhere.

1. Click **Save**.

## Configure a universe to use OIDC

You enable OIDC for universes by setting authentication flags for YSQL database access. The database uses PostgreSQL `yb_hba.conf` and `yb_ident.conf` files to translate authentication rules into database roles.

For information on configuring flags in YugabyteDB Anywhere, refer to [Edit configuration flags](../../../manage-deployments/edit-config-flags/).

To access a universe via OIDC, set the following flags on the universe:

- ysql_hba_conf_csv
- ysql_ident_conf_csv

When the flags are set, YugabyteDB configures the `ysql_hba.conf` and `yb_ident.conf` files on the database nodes and creates the files that hold the JWKS keys for token validation.

#### ysql_hba_conf_csv

The `ysql_hba_conf_csv` flag must be set to support using JWTs for authentication. The parameters to include in the configuration file record are described in the following table:

| Parameter | Description |
| :-------- | :---------- |
| `map` | The user-name map used to translate claim values to database roles. Optional if you aren't using the default Subject claim values. |
| `jwt_issuers` | The first part of the discovery URL.<br>Azure: `login.microsoftonline.com/<tenant_id>/v2.0`<br>JumpCloud: `https://oauth.id.jumpcloud.com/` |
| `jwt_audiences` | The audience or target app for the token, which in this case is the client ID of the application you registered. |
| `jwt_matching_claim_key` | The email attribute you set (for example, `preferred_username`). Optional if you aren't using the default Subject claim values. |
| `jwt_jwks_path` | The JSON Web Key Set (JWKS) is a set of keys containing the public keys used to verify any JWT. These can be uploaded as entries in a single file. When configuring the flag in YugabyteDB Anywhere, click **Add JSON web key set (JWKS)** to upload the JWKS. |
| `jwt_jwks_url` | The URL where YugabyteDB can retrieve the JWKS for verifying JWTs. This parameter is an alternative to `jwt_jwks_path`. You must set either `jwt_jwks_path` or `jwt_jwks_url` to enable JWT verification in YugabyteDB. |

The following illustration shows an example of setting the `ysql_hba_conf_csv` flag in YugabyteDB Anywhere:

![Configuring ysql_hba_conf_csv flag for OIDC](/images/yp/security/oidc-azure-hbaconf.png)

The following shows an example `ysql_hba_conf_csv` flag configuration for Azure:

```sh
host all all 0.0.0.0/0 jwt map=map1 jwt_audiences=""<client_id>"" jwt_issuers=""https://login.microsoftonline.com/<tenant_id>/v2.0"" jwt_matching_claim_key=""preferred_username""
```

The following shows an example `ysql_hba_conf_csv` flag configuration for JumpCloud:

```sh
host all all 0.0.0.0/0 jwt map=map1 jwt_audiences=""<client_id>"" jwt_issuers=""https://oauth.id.jumpcloud.com/"" jwt_matching_claim_key=""preferred_username""
```

For more information on host authentication in YugabyteDB using `ysql_hba_conf_csv`, refer to [Host-based authentication](../../../../secure/authentication/host-based-authentication/).

#### ysql_ident_conf_csv

This flag is used to add translation regex rules that map token claim values to PostgreSQL roles. The flag settings are used as records in the `yb_ident.conf` file as user-name maps. This file is used identically to `pg_ident.conf` to map external identities to database users. For more information, refer to [User name maps](https://www.postgresql.org/docs/15/auth-username-maps.html) in the PostgreSQL documentation.

The following illustration shows an example flag configuration:

![Configuring ysql_ident_conf_csv flag for OIDC](/images/yp/security/oidc-azure-identconf.png)

The following are examples of possible rules:

- Map a single user

  ```sh
  map1 user@yugabyte.com user
  ```

- Map multiple users

  ```sh
  map2 /^(.*)@devadmincloudyugabyte\.onmicrosoft\.com$ \1
  ```

- Map Roles <-> Users

  ```sh
  map1 OIDC.Test.Read read_only_user
  ```

## Manage users and roles

After OIDC-based authentication is configured, an administrator can manage users as follows:

- In the universe, add database users or roles.

  You need to add the users and roles that will be used to authenticate to the database. The role must be assigned the appropriate permissions in advance. Users will use their database user/role as their username credential along with their JWT as the password when connecting to the universe.

  For information on managing users and roles in YugabyteDB, see [Manage users and roles](../../../../secure/authorization/create-roles/).

- In YugabyteDB Anywhere, create YugabyteDB Anywhere users.

  Create a user in YugabyteDB Anywhere for each user who wishes to sign in to YugabyteDB Anywhere to obtain their JWT.

  To view their JWT, YugabyteDB Anywhere users can sign in to YugabyteDB Anywhere, click the **User** icon at the top right, select **User Profile**, and click **Fetch OIDC Token**.

  This is not required if you enabled the **Display JWT token on login** option in the YugabyteDB Anywhere OIDC configuration, as any database user can copy the JWT from the YugabyteDB Anywhere landing page without signing in to YugabyteDB Anywhere.

  For information on how to add YugabyteDB Anywhere users, see [Manage YugabyteDB Anywhere users](../../../administer-yugabyte-platform/anywhere-rbac/).

## Using your JWT

If the administrator has enabled the **Display JWT token on login** setting, you can obtain your token from the YugabyteDB Anywhere landing page. Click **Fetch JSON Web Token**; you are redirected to the IdP to enter your credentials as required. You are then redirected back to YugabyteDB Anywhere, where your token is displayed, along with the expiration time of the token.

Use the token as your password to access the database. Your username will match the database username/role that was assigned to you by your administrator.

## Learn more

- [Enable YugabyteDB Anywhere authentication via OIDC](../../../administer-yugabyte-platform/oidc-authentication/)
- [YFTT: OIDC Authentication in YSQL](https://www.youtube.com/watch?v=KJ0XV6OnAnU&list=PL8Z3vt4qJTkLTIqB9eTLuqOdpzghX8H40&index=1)