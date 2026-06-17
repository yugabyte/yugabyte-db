---
title: OIDC authentication using JumpCloud in YugabyteDB Anywhere
headerTitle: OIDC authentication with JumpCloud
linkTitle: OIDC authentication
description: Configuring YugabyteDB Anywhere universe to use OIDC with JumpCloud.
headcontent: Use JumpCloud to authenticate accounts for database access
menu:
  stable_yugabyte-platform:
    identifier: oidc-authentication-jumpcloud
    parent: authentication
    weight: 30
type: docs
rightNav:
  hideH4: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../oidc-authentication-aad/" class="nav-link">
      Azure AD
    </a>
  </li>
  <li >
    <a href="../oidc-authentication-jumpcloud/" class="nav-link active">
      JumpCloud
    </a>
  </li>
</ul>

This section describes how to configure a YugabyteDB Anywhere (YBA) universe to use OIDC-based authentication for YugabyteDB YSQL and YCQL database access using JumpCloud as the example Identity Provider (IdP), the integration works with any OIDC-compliant provider.

After OIDC is set up, users can sign in to the YugabyteDB universe database using their JSON Web Token (JWT) as their password.

Note that the yugabyte privileged user will continue to exist as a local database user even after OIDC-based authentication is enabled for a universe.

**Learn more**

- [Enable YugabyteDB Anywhere authentication via OIDC](../../../administer-yugabyte-platform/oidc-authentication/)
- [YFTT: OIDC Authentication in YSQL](https://www.youtube.com/watch?v=KJ0XV6OnAnU&list=PL8Z3vt4qJTkLTIqB9eTLuqOdpzghX8H40&index=1)

## OIDC callback URI

YugabyteDB Anywhere supports callback (redirect) URIs in one of the following formats:

- Query (default):

    `https://<YBA_IP_Address>/api/v1/callback?client_name=OidcClient`

- Path:

    `https://<YBA_IP_Address>/api/v1/callback/OidcClient`

    Note that Path is only available in v2025.2.4.0 and later.

This is where the IdP redirects after authentication.

Only one format is supported at a time. To change the URI format, set the **OIDC Callback Mode** Global Runtime Configuration option (config key `yb.security.oidc_callback_mode`). Refer to [Manage runtime configuration settings](../../../administer-yugabyte-platform/manage-runtime-config/). You must be a Super Admin to set global runtime configuration flags.

## Set up OIDC with JumpCloud on YugabyteDB Anywhere

To enable OIDC authentication with JumpCloud, you need to do the following:

- Create an app registration in JumpCloud - The JumpCloud IdP configuration includes application registration (registering YugabyteDB Anywhere in the JumpCloud tenant) and configuring JumpCloud to send (redirect) tokens with the required claims to YugabyteDB Anywhere.
- Configure OIDC in YugabyteDB Anywhere - The OIDC configuration uses the application you registered. You can also configure YBA to display the user's JSON Web Token (JWT) on the sign in screen.
- Configure the universe to use OIDC - You enable OIDC for universes by setting authentication flags for YSQL or YCQL database access. For YSQL, the database uses PostgreSQL `yb_hba.conf` and `yb_ident.conf` files to translate authentication rules into database roles. For YCQL, you set YB-TServer flags such as `ycql_jwt_conf` and optional `ycql_ident_conf_csv` identity mapping rules.

### Create an application in JumpCloud

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

    - **Redirect URIs**. Enter the [OIDC callback URI](#oidc-callback-uri). This is where the IdP redirects after authentication.
    - **Client Authentication Type**. Select **Client Secret Post**.
    - **Login URL**. Enter `https://<your-YugabyteDB-Anywhere-IP-address>/login`.

    Under **Attribute Mapping**, for **Standard Scopes**, select **Email** and **Profile**.

    Click **Activate** when you are done.

    You will be prompted in a pop up to save the **Client ID** and **Client Secret**. Save these in a secure location, you will need to provide these credentials in YugabyteDB Anywhere.

1. Configure Attributes and Identity Management as required.

1. Integrate the user in JumpCloud.

    - Navigate to **User Groups**, select the user groups you want to access YugabyteDB Anywhere, and click **Save** when you are done.

To [configure](#configure-yugabytedb-anywhere) JumpCloud federated authentication in YugabyteDB Anywhere, you need the following application properties:

- **Client ID** and **Client Secret** of the application you created. These are the credentials you saved when you activated your application. The **Client ID** is also displayed on the **SSO** tab.

For more information, refer to the [JumpCloud](https://jumpcloud.com/support/sso-with-oidc) documentation.

### Configure YugabyteDB Anywhere

To configure YugabyteDB Anywhere for OIDC, you need to be signed in as a Super Admin. You need your JumpCloud application client ID and client secret.

To allow users to access their JWT from the YugabyteDB sign in page, you must enable the OIDC feature via a configuration flag before you configure OIDC.

#### Enable OIDC enhancements

To enable some features of the OIDC functionality in Yugabyte Anywhere, you need to set the `yb.security.oidc_feature_enhancements` configuration flag to true as follows:

1. Navigate to **Admin > Advanced > Global Configuration**.

1. Search on OIDC to display the configuration setting and set it to true.

    ![Configuring yb.security.oidc_feature_enhancements flag for OIDC](/images/yp/security/oidc-azure-globalfeature.png)

#### Enable OIDC authentication

To configure User authentication in YugabyteDB Anywhere, do the following:

1. Navigate to **Admin > Access Management > User Authentication** and select **ODIC configuration**.
1. Under **OIDC configuration**,  configure the following:

    - **Client ID** and **Client Secret** - enter the client ID and secret of the JumpCloud application you created.
    - **Discovery URL** - enter `https://oauth.id.jumpcloud.com/.well-known/openid-configuration`.
    - **Scope** - enter `openid email`. If you are using the Refresh Token feature to allow the Jumpcloud server to return the refresh token (which can be used by YBA to refresh the login), enter `openid offline_access profile email` instead.
    - **Email attribute** - enter your registered email.
    - **Refresh Token URL** - if you have configured OIDC to use [refresh tokens](https://openid.net/specs/openid-connect-core-1_0.html#RefreshTokens), in the **Refresh Token URL** field, enter the URL of the refresh token endpoint.
    - **Display JWT token on login** - select this option to allow users to access their JWT from the YugabyteDB Anywhere sign in page. This allows a user to view and copy their JWT without signing in to YBA. (This option is only available if you enabled the `yb.security.oidc_feature_enhancements` configuration flag.)

1. Click **Save**.

You are redirected to sign in to your IdP to test the connection. After the test connection is successful, OIDC authentication is enabled.

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

The `ysql_hba_conf_csv` flag must be set to support using JWTs for authentication. The parameters to include in the configuration file record are as follows:

- `map` - the user-name map used to translate claim values to database roles. Optional if you aren't using the default Subject claim values.
- `jwt_issuers` - the first part of the discovery URL (`https://oauth.id.jumpcloud.com/`)
- `jwt_audiences` - the audience or target app for the token, which in this case is the client ID of the application you registered.
- `jwt_matching_claim_key` - the email attribute you set (for example, `preferred_username`). Optional if you aren't using the default Subject claim values.
- `jwt_jwks_path` - The JSON Web Key Set (JWKS) is a set of keys containing the public keys used to verify any JWT. These can be uploaded as entries in a single file. When configuring the flag in YugabyteDB Anywhere, click **Add JSON web key set (JWKS)** to upload the JWKS.

The following illustration shows an example of setting the `ysql_hba_conf_csv` flag in YugabyteDB Anywhere:

![Configuring ysql_hba_conf_csv flag for OIDC](/images/yp/security/oidc-jumpcloud-hbaconf.png)

The following shows an example `ysql_hba_conf_csv` flag configuration for OIDC:

```sh
host all all 0.0.0.0/0 jwt map=map1 jwt_audiences=""<client_id>"" jwt_issuers=""https://login.microsoftonline.com/<tenant_id>/v2.0"" jwt_matching_claim_key=""preferred_username""
```

For more information on host authentication in YugabyteDB using `ysql_hba_conf_csv`, refer to [Host-based authentication](../../../../secure/authentication/host-based-authentication/).

#### ysql_ident_conf_csv

This flag is used to add translation regex rules that map token claim values to PostgreSQL roles. The flag settings are used as records in the `yb_ident.conf` file as user-name maps. This file is used identically to `pg_ident.conf` to map external identities to database users. For more information, refer to [User name maps](https://www.postgresql.org/docs/15/auth-username-maps.html) in the PostgreSQL documentation.

The following illustration shows an example flag configuration:

![Configuring ysql_ident_conf_csv flag for OIDC](/images/yp/security/oidc-jumpcloud-identconf.png)

The following are examples of possible rules:

- Map a single user

  ```sh
  map1 user@yugabyte.com user
  ```

- Map multiple users

  ```sh
  map2 /^(.*)@devyugabyte\.com$ \1
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

### Prerequisites

OIDC for YCQL requires YCQL authentication to be enabled on the universe. When you turn on YCQL authorization in YugabyteDB Anywhere, YBA sets the [`use_cassandra_authentication`](../../../../reference/configuration/yb-tserver/#use-cassandra-authentication) flag automatically; do not set this flag manually via **Edit Flags**.

To enable YCQL authorization when creating or modifying a universe, refer to [Enable database endpoints and authorization](../../authorization-platform/#enable-database-endpoints-and-authorization) and [Modify endpoint configuration](../../authorization-platform/#modify-endpoint-configuration).

### Set OIDC YB-TServer flags

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

The following shows an example OIDC flag configuration for JumpCloud (without identity mapping). Set these flags in **Edit Flags**; `use_cassandra_authentication` is included for reference only and is set automatically when [YCQL authentication is enabled](../../authorization-platform/#modify-endpoint-configuration).

```sh
use_cassandra_authentication=true
ycql_use_jwt_auth=true
ycql_jwt_conf={jwt_jwks_url=https://oauth.id.jumpcloud.com/jwks jwt_audiences=<client_id> jwt_issuers=https://oauth.id.jumpcloud.com/ jwt_matching_claim_key=preferred_username}
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
  /^(.*)@devyugabyte\.com$ \1
  ```

- Map a role name to a database role

  ```sh
  OIDC.Test.Read read_only_user
  ```

The following shows an example flag configuration for OIDC with JumpCloud and identity mapping (role-based usernames):

```sh
use_cassandra_authentication=true
ycql_use_jwt_auth=true
ycql_jwt_conf={jwt_jwks_url=https://oauth.id.jumpcloud.com/jwks jwt_audiences=<client_id> jwt_issuers=https://oauth.id.jumpcloud.com/ jwt_matching_claim_key=roles}
ycql_jwt_users_to_skip_csv=cassandra
ycql_ident_conf_csv={/^(.*)@devyugabyte\.com$ \1}
```

With `jwt_matching_claim_key=roles`, YCQL reads identities from the `roles` claim. The `ycql_ident_conf_csv` rule maps each matching role string to the local part before `@devyugabyte.com`, which must match an existing YCQL role name.

</div>

</div>

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

{{< readfile "/stable/yugabyte-platform/security/authentication/oidc-manage-users-include.md" >}}

**Learn more**

- [Enable YugabyteDB Anywhere authentication via OIDC](../../../administer-yugabyte-platform/oidc-authentication/)
- [YFTT: OIDC Authentication in YSQL](https://www.youtube.com/watch?v=KJ0XV6OnAnU&list=PL8Z3vt4qJTkLTIqB9eTLuqOdpzghX8H40&index=1)