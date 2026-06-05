---
title: OIDC authentication using Azure AD in YugabyteDB Anywhere
headerTitle: OIDC authentication with Azure AD
linkTitle: OIDC authentication
description: Configuring YugabyteDB Anywhere universe to use OIDC with Microsoft Entra.
headcontent: Use Azure AD to authenticate accounts for database access
menu:
  stable_yugabyte-platform:
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

This section describes how to configure a YugabyteDB Anywhere (YBA) universe to use OIDC-based authentication for YugabyteDB YSQL and YCQL database access using Azure AD (also known as [Microsoft Entra ID](https://www.microsoft.com/en-ca/security/business/identity-access/microsoft-entra-id)) as the Identity Provider (IdP).

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
- Configure the universe to use OIDC - You enable OIDC for universes by setting authentication flags for YSQL or YCQL database access. For YSQL, the database uses PostgreSQL `yb_hba.conf` and `yb_ident.conf` files to translate authentication rules into database roles. For YCQL, you set YB-TServer flags such as `ycql_jwt_conf` and optional `ycql_ident_conf_csv` identity mapping rules.

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

#### Enable OIDC authentication

To enable OIDC authentication in YugabyteDB Anywhere, do the following:

1. Navigate to **Admin > Access Management > User Authentication** and select **ODIC configuration**.
1. Under **OIDC configuration**,  configure the following:

    - **Client ID** and **Client Secret** - enter the client ID and secret of the Azure application you created.
    - **Discovery URL** - enter `login.microsoftonline.com/<tenant_id>/v2.0/.well-known/openid-configuration`.
    - **Scope** - enter `openid email profile`. If you are using the Refresh Token feature to allow the Azure server to return the refresh token (which can be used by YBA to refresh the login), enter `openid offline_access profile email` instead.
    - **Email attribute** - enter the email attribute to a name for the property to be used in the mapping file, such as `preferred_username`.
    - **Refresh Token URL** - if you have configured OIDC to use [refresh tokens](https://openid.net/specs/openid-connect-core-1_0.html#RefreshTokens), in the **Refresh Token URL** field, enter the URL of the refresh token endpoint.
    - **Display JWT token on login** - select this option to allow users to access their JWT from the YugabyteDB Anywhere sign in page. This allows a user to view and copy their JWT without signing in to YBA.

1. Click **Save**.

### Configure a universe

To access a universe via OIDC, set the flags described in the following tabs for YSQL or YCQL.

For information on configuring flags in YugabyteDB Anywhere, refer to [Edit configuration flags](../../../manage-deployments/edit-config-flags/).

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#oidc-ysql" class="nav-link active" id="oidc-ysql-tab" data-bs-toggle="tab" role="tab" aria-controls="oidc-ysql" aria-selected="true">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li>
    <a href="#oidc-ycql" class="nav-link" id="oidc-ycql-tab" data-bs-toggle="tab" role="tab" aria-controls="oidc-ycql" aria-selected="false">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

<div class="tab-content">

<div id="oidc-ysql" class="tab-pane fade show active" role="tabpanel" aria-labelledby="oidc-ysql-tab">

To access a universe via OIDC for YSQL, set the following flags on the universe:

- ysql_hba_conf_csv
- ysql_ident_conf_csv

When the flags are set, YugabyteDB configures the `ysql_hba.conf` and `yb_ident.conf` files on the database nodes and creates the files that hold the JWKS keys for token validation.

#### ysql_hba_conf_csv

The `ysql_hba_conf_csv` flag must be set to support using JWTs for authentication. The parameters to include in the configuration file record are described in the following table:

| Parameter | Description |
| :-------- | :---------- |
| `jwt_map` | The user-name map used to translate claim values to database roles. Optional if you aren't using the default Subject claim values. |
| `jwt_issuers` | The first part of the discovery URL (`login.microsoftonline.com/<tenant_id>/v2.0`). |
| `jwt_audiences` | The audience or target app for the token, which in this case is the client ID of the application you registered. |
| `jwt_matching_claim_key` | The email attribute you set (for example, `preferred_username`). Optional if you aren't using the default Subject claim values. |
| `jwt_jwks_path` | The JSON Web Key Set (JWKS) is a set of keys containing the public keys used to verify any JWT. These can be uploaded as entries in a single file. When configuring the flag in YugabyteDB Anywhere, click **Add JSON web key set (JWKS)** to upload the JWKS. |
|  `jwt_jwks_url` | The URL where YugabyteDB can retrieve the JWKS for verifying JWTs. This parameter is an alternative to `jwt_jwks_path`. You must set either `jwt_jwks_path` or `jwt_jwks_url` to enable JWT verification in YugabyteDB. |

The following illustration shows an example of setting the `ysql_hba_conf_csv` flag in YugabyteDB Anywhere:

![Configuring ysql_hba_conf_csv flag for OIDC](/images/yp/security/oidc-azure-hbaconf.png)

The following shows an example `ysql_hba_conf_csv` flag configuration for OIDC:

```sh
host all all 0.0.0.0/0 jwt_map=map1 jwt_audiences=""<client_id>"" jwt_issuers=""https://login.microsoftonline.com/<tenant_id>/v2.0"" jwt_matching_claim_key=""preferred_username""
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

</div>

<div id="oidc-ycql" class="tab-pane fade" role="tabpanel" aria-labelledby="oidc-ycql-tab">

To access a universe via OIDC for YCQL, set OIDC-related YB-TServer flags on the universe. Depending on your requirements, you can configure OIDC in two ways:

- _Without identity mapping_ between the IdP and the YCQL user.
- _With identity mapping_ between the IdP and the YCQL user (requires `ycql_ident_conf_csv`, described in [ycql_ident_conf_csv](#ycql-ident-conf-csv)).

Configuring OIDC for YCQL requires two steps: enable YCQL authentication in the UI, then set the OIDC-related YB-TServer flags together in **Edit Flags**.

#### Step 1: Enable YCQL authentication

YugabyteDB Anywhere sets the [`use_cassandra_authentication`](../../../../reference/configuration/yb-tserver/#use-cassandra-authentication) flag automatically when you enable YCQL authorization in the UI. Do not set this flag manually via **Edit Flags**.

To enable YCQL authorization on an existing universe:

1. Navigate to your universe and click **Actions > More > Edit YCQL Configuration**.
1. Turn on **Enable YCQL for this Universe**, then turn on **Enable YCQL Auth**.
1. Optionally, for non-Kubernetes universes, turn on **Override YCQL Ports** and set **YCQL HTTP Port** and **YCQL Port** if you need custom endpoint ports (defaults are 12000 and 9042, respectively). These settings are not required for OIDC.
1. Click **Apply Changes**.

The **Edit YCQL Configuration** dialog also lets you rotate the `cassandra` superuser password when YCQL Auth is enabled. For more information on these settings, refer to [Modify endpoint configuration](../../authorization-platform/#modify-endpoint-configuration).

You can also enable YCQL authorization when [creating a universe](../../../create-deployments/create-universe-multi-zone/) under **Security Configurations > Authentication Settings**. For more information, refer to [Enable database endpoints and authorization](../../authorization-platform/#enable-database-endpoints-and-authorization).

#### Step 2: Set OIDC YB-TServer flags

Add all OIDC-related flags to YB-TServer in a single **Actions > Edit Flags** session. For more information, refer to [Edit configuration flags](../../../manage-deployments/edit-config-flags/).

1. Navigate to your universe and click **Actions > Edit Flags**.
1. Add the following flags to YB-TServer (and `ycql_ident_conf_csv` if you are using identity mapping).
1. Apply the changes.

Use the following flags to configure OIDC for YCQL.

| YB-TServer flag | Default | Description |
| :-------------- | :------ | :---------- |
| `ycql_use_jwt_auth` | `false` | Enables OIDC (JWT) authentication in YCQL. |
| `ycql_jwt_users_to_skip_csv` | empty | Comma-separated list of users that continue to use the local password mechanism even when `ycql_use_jwt_auth` is `true`. |
| `ycql_jwt_conf` | empty | Space-separated list of `key=value` options that configure JWT validation and which claim identifies the user. Valid keys are listed in the next table. |

Valid keys for `ycql_jwt_conf` are described in the following table:

| Key | Description |
| :-- | :---------- |
| `jwt_jwks_url` | URL from which to fetch the JSON Web Key Set (JWKS) of the IdP. |
| `jwt_audiences` | A list of accepted audiences for the token. A JWT is valid only if the `aud` claim equals one of the values in this list. Multiple values are comma-separated. For more information, see the [aud claim](https://openid.net/specs/openid-connect-core-1_0.html#IDToken) in the OpenID Connect Core specification. |
| `jwt_issuers` | Comma-separated list of valid issuers. A JWT is valid only if the `iss` claim equals one of these values. For more information, see the [iss claim](https://openid.net/specs/openid-connect-core-1_0.html#IDToken) in the OpenID Connect Core specification. |
| `jwt_matching_claim_key` | Key of the JWT claim that carries the IdP identity used for login (for example, `sub`, `email`, `groups`, or `roles`). Optional. Default is `sub`. |

After these options are configured, the JWT from the IdP is supplied as the password when connecting to YCQL.

The following shows an example OIDC flag configuration for Azure AD (without identity mapping). Set these flags in **Edit Flags**; `use_cassandra_authentication` is included for reference only and is set automatically by [Step 1](#step-1-enable-ycql-authentication).

```sh
use_cassandra_authentication=true
ycql_use_jwt_auth=true
ycql_jwt_conf={jwt_jwks_url=https://login.microsoftonline.com/<tenant_id>/discovery/v2.0/keys jwt_audiences=<client_id> jwt_issuers=https://login.microsoftonline.com/<tenant_id>/v2.0 jwt_matching_claim_key=preferred_username}
ycql_jwt_users_to_skip_csv=cassandra
```

The `ycql_jwt_users_to_skip_csv=cassandra` setting allows the `cassandra` user to continue using password authentication so an administrator can sign in to create roles and permissions for OIDC users.

When entering flag values in YugabyteDB Anywhere, do not enclose them in single quotes, as you would in a Linux shell.

For more information, refer to [OIDC authentication in YCQL](../../../../secure/authentication/oidc-authentication-ycql/).

#### ycql_ident_conf_csv

Without identity mapping, the YCQL user name must match the IdP identity given by `jwt_matching_claim_key` (that is, `JWT[jwt_matching_claim_key]`).

To allow different user names or to support group- or role-based authentication, configure an identity mapping between IdP identities and YCQL roles using the following flag:

| YB-TServer flag | Default | Description |
| :-------------- | :------ | :---------- |
| `ycql_ident_conf_csv` | empty | CSV formatted list of identity mapping rules, evaluated in order. Each rule contains two fields separated by whitespace: the IdP user name pattern and the YCQL user name. |

Identity mapping rules are similar to PostgreSQL [user name maps](https://www.postgresql.org/docs/15/auth-username-maps.html), where each rule has the form `idp-username database-username`. Separate rules using commas in the CSV flag value.

If `idp-username` starts with `/`, the remainder of the field is treated as a regular expression. The expression can contain a single capture group. The portion of the IdP user name that matched the capture can then be referenced in the database-username field as `\1` (backslash-one).

The following are examples of possible rules:

- Map a single user

  ```sh
  user@yugabyte.com user
  ```

- Map multiple users with a domain pattern

  ```sh
  /^(.*)@devadmincloudyugabyte\.onmicrosoft\.com$ \1
  ```

- Map a role name to a database role

  ```sh
  OIDC.Test.Read read_only_user
  ```

The following shows an example flag configuration for OIDC with Azure AD and identity mapping (role-based usernames):

```sh
use_cassandra_authentication=true
ycql_use_jwt_auth=true
ycql_jwt_conf={jwt_jwks_url=https://login.microsoftonline.com/<tenant_id>/discovery/v2.0/keys jwt_audiences=<client_id> jwt_issuers=https://login.microsoftonline.com/<tenant_id>/v2.0 jwt_matching_claim_key=roles}
ycql_jwt_users_to_skip_csv=cassandra
ycql_ident_conf_csv={/^(.*)@company\.com$ \1}
```

With `jwt_matching_claim_key=roles`, YCQL reads identities from the `roles` claim. The `ycql_ident_conf_csv` rule maps each matching role string to the local part before `@company.com`, which must match an existing YCQL role name.

</div>

</div>

## Manage users and roles

{{< readfile "/stable/yugabyte-platform/security/authentication/oidc-manage-users-include.md" >}}
