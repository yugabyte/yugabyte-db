---
title: OIDC authentication using Azure AD in YugabyteDB Anywhere
headerTitle: OIDC authentication with Azure AD
linkTitle: OIDC authentication
description: Configuring YugabyteDB Anywhere universe to use OIDC with Microsoft Entra.
headcontent: Use Azure AD to authenticate accounts for database access
badges: ea
menu:
  preview_yugabyte-platform:
    identifier: oidc-authentication-platform
    parent: authentication
    weight: 20
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../oidc-authentication-aad/" class="nav-link active">
      Azure AD
    </a>
  </li>
  <li >
    <a href="../oidc-authentication-jumpcloud/" class="nav-link">
      JumpCloud
    </a>
  </li>
</ul>

This section describes how to configure a YugabyteDB Anywhere (YBA) universe to use OIDC-based authentication for YugabyteDB YSQL database access using Azure AD (also known as [Microsoft Entra ID](https://www.microsoft.com/en-ca/security/business/identity-access/microsoft-entra-id)) as the Identity Provider (IdP).

After OIDC is set up, users can sign in to the YugabyteDB universe database using their JSON Web Token (JWT) as their password.

Note that the yugabyte privileged user will continue to exist as a local database user even after OIDC-based authentication is enabled for a universe.

**Learn more**

- [Enable YugabyteDB Anywhere authentication via OIDC](../../../administer-yugabyte-platform/oidc-authentication/)
- [YFTT: OIDC Authentication in YSQL](https://www.youtube.com/watch?v=KJ0XV6OnAnU&list=PL8Z3vt4qJTkLTIqB9eTLuqOdpzghX8H40&index=1)

## Group claims and roles in Azure AD

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

```json.output
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

```json.output
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

## Set up OIDC with Azure AD on YugabyteDB Anywhere

To enable OIDC authentication with Azure AD, you need to do the following:

- Create an app registration in Azure AD - The Azure AD IdP configuration includes application registration (registering YugabyteDB Anywhere in the Azure AD tenant) and configuring Azure AD to send (redirect) tokens with the required claims to YugabyteDB Anywhere.
- Configure OIDC in YugabyteDB Anywhere - The OIDC configuration uses the application you registered. You can also configure YBA to display the user's JSON Web Token (JWT) on the sign in screen.
- Configure the universe to use OIDC - You enable OIDC for universes by setting authentication rules for database access using flags. The database is implicitly configured and picks up the authentication rules you set. The database uses well-known PostgreSQL constructs to translate these authentication rules into database roles for access. Mapping Azure AD attributes, such as group memberships, roles, and email addresses to database roles, is accomplished using the PostgreSQL `yb_hba.conf` and `yb_ident.conf` files.

### Register an application in Azure

You register your YugabyteDB Anywhere instance as an application in Azure. You will use the application's tenant ID, client ID, and client secret to configure OIDC in YugabyteDB Anywhere.

To register an application, do the following:

1. In the Azure console, navigate to **App registrations** and click **New registration**.

1. Enter a name for the application.

1. Select the tenant for the application.

1. Set the redirect URI. This is where the IdP redirects after authentication. The URI is in the following form:

    ```sh
    https://<YBA_IP_Address>/api/v1/callback?client_name=OidcClient
    ```

1. Click **Register**.

    After the application is registered, you can obtain the tenant ID and client ID.

1. Click **Add a certificate or secret**.

1. Select **Client secrets** and click **New client secret**.

1. Enter a description and set the expiry for the secret, and then click **Add**.

1. Copy the secret value and keep it in a secure location.

For more information, refer to [Register an application with the Microsoft identity platform](https://learn.microsoft.com/en-us/entra/identity-platform/quickstart-register-app) in the Azure documentation.

### Configure YugabyteDB Anywhere

To configure YugabyteDB Anywhere for OIDC, you need to be signed in as a Super Admin. You need your Azure application client ID, client secret, and tenant ID.

To allow users to access their JWT from the YugabyteDB sign in page, you must enable the OIDC feature via a configuration flag before you configure OIDC.

#### Enable OIDC enhancements

To enable some features of the OIDC functionality in Yugabyte Anywhere, you need to set the `yb.security.oidc_feature_enhancements` configuration flag to true as follows:

1. Navigate to **Admin > Advanced > Global Configuration**.

1. Search on OIDC to display the configuration setting and set it to true.

    ![Configuring yb.security.oidc_feature_enhancements flag for OIDC](/images/yp/security/oidc-azure-globalfeature.png)

#### Enable OIDC authentication

To enable OIDC authentication in YugabyteDB Anywhere, do the following:

1. Navigate to **Admin > Access Management > User Authentication** and select **ODIC configuration**.
1. Under **OIDC configuration**,  configure the following:

    - **Client ID** and **Client Secret** - enter the client ID and secret of the Azure application you created.
    - **Discovery URL** - enter `login.microsoftonline.com/<tenant_id>/v2.0/.well-known/openid-configuration`.
    - **Scope** - enter `openid email profile`. If you are using the Refresh Token feature to allow the Azure server to return the refresh token (which can be used by YBA to refresh the login), enter `openid offline_access profile email` instead.
    - **Email attribute** - enter the email attribute to a name for the property to be used in the mapping file, such as `preferred_username`.
    - **Refresh Token URL** - if you have configured OIDC to use [refresh tokens](https://openid.net/specs/openid-connect-core-1_0.html#RefreshTokens), in the **Refresh Token URL** field, enter the URL of the refresh token endpoint.
    - **Display JWT token on login** - select this option to allow users to access their JWT from the YugabyteDB Anywhere sign in page. This allows a user to view and copy their JWT without signing in to YBA. (This option is only available if you enabled the `yb.security.oidc_feature_enhancements` configuration flag.)

1. Click **Save**.

### Configure a universe

To access a universe via OIDC, you need to set the following flags on the universe:

- ysql_hba_conf_csv
- ysql_ident_conf_csv

When the flags are set, YugabyteDB configures the `ysql_hba.conf` and `yb_ident.conf` files on the database nodes and creates the files that hold the JWKS keys for token validation.

For information on configuring flags in YugabyteDB Anywhere, refer to [Edit configuration flags](../../../manage-deployments/edit-config-flags/).

#### ysql_hba_conf_csv

The `ysql_hba_conf_csv` flag must be set to support using JWTs for authentication. The parameters to include in the configuration file record are as follows:

- `jwt_map` - the user-name map used to translate claim values to database roles. Optional if you aren't using the default Subject claim values.
- `jwt_issuers` - the first part of the discovery URL (`login.microsoftonline.com/<tenant_id>/v2.0`)
- `jwt_audiences` - the audience or target app for the token, which in this case is the client ID of the application you registered.
- `jwt_matching_claim_key` - the email attribute you set (for example, `preferred_username`). Optional if you aren't using the default Subject claim values.
- `jwt_jwks_path` - The JSON Web Key Set (JWKS) is a set of keys containing the public keys used to verify any JWT. These can be uploaded as entries in a single file. When configuring the flag in YugabyteDB Anywhere, click **Add JSON web key set (JWKS)** to upload the JWKS.

The following illustration shows an example of setting the `ysql_hba_conf_csv` flag in YugabyteDB Anywhere:

![Configuring ysql_hba_conf_csv flag for OIDC](/images/yp/security/oidc-azure-hbaconf.png)

The following shows an example `ysql_hba_conf_csv` flag configuration for OIDC:

```sh
host all all 0.0.0.0/0 jwt_map=map1 jwt_audiences=""<client_id>"" jwt_issuers=""https://login.microsoftonline.com/<tenant_id>/v2.0"" jwt_matching_claim_key=""preferred_username""
```

For more information on host authentication in YugabyteDB using `ysql_hba_conf_csv`, refer to [Host-based authentication](../../../../secure/authentication/host-based-authentication/).

#### ysql_ident_conf_csv

This flag is used to add translation regex rules that map token claim values to PostgreSQL roles. The flag settings are used as records in the `yb_ident.conf` file as user-name maps. This file is used identically to `pg_ident.conf` to map external identities to database users. For more information, refer to [User name maps](https://www.postgresql.org/docs/11/auth-username-maps.html) in the PostgreSQL documentation.

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

#### yb.security.oidc_feature_enhancements

This flag must be enabled to expose the OIDC functionality in Yugabyte Anywhere. Use the following API to set values for this flag.

```sh
curl -k --location --request PUT '<server-address>/api/v1/customers/<customerUUID>/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.security.oidc_feature_enhancements' \
--header 'Content-Type: text/plain' \
--header 'Accept: application/json' \
--header 'X-AUTH-YW-API-TOKEN: <api-token>' \
--data 'true'
```

## Manage users and roles

{{< readfile "/preview/yugabyte-platform/security/authentication/oidc-manage-users-include.md" >}}
