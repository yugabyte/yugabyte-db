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
    weight: 30
type: docs
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

    - **Redirect URIs** - enter `https://<your-YugabyteDB-Anywhere-IP-address>/api/v1/callback?client_name=OidcClient`.
    - **Client Authentication Type** - select **Client Secret Post**.
    - **Login URL** - enter `https://<your-YugabyteDB-Anywhere-IP-address>/login`.

    Under **Attribute Mapping**, for **Standard Scopes**, select **Email** and **Profile**.

    Click **Activate** when you are done.

    You will be prompted in a pop up to save the **Client ID** and **Client Secret**. Save these in a secure location, you will need to provide these credentials in YugabyteDB Anywhere.

1. Configure Attributes and Identity Management as required.

1. Integrate the user in JumpCloud.

    - Navigate to **User Groups**, select the user groups you want to access YugabyteDB Aeon, and click **Save** when you are done.

To configure JumpCloud federated authentication in YugabyteDB Aeon, you need the following application properties:

- **Client ID** and **Client Secret** of the application you created. These are the credentials you saved when you activated your application. The **Client ID** is also displayed on the **SSO** tab.

For more information, refer to the [JumpCloud](https://jumpcloud.com/support/sso-with-oidc) documentation.

### Configure YugabyteDB Anywhere

#### Enable OIDC authentication

To configure User authentication in YugabyteDB Anywhere, do the following:

1. Navigate to **Admin > Access Management > User Authentication** and select **ODIC configuration**.
1. Under **OIDC configuration**,  configure the following:

    - **Client ID** and **Client Secret** - enter the client ID and secret of the JumpCloud application you created.
    - **Discovery URL** - enter `https://oauth.id.jumpcloud.com/.well-known/openid-configuration`.
    - **Scope** - enter `openid email`.
    - **Email attribute** - enter your registered email.

1. Click **Save**.

You are redirected to sign in to your IdP to test the connection. After the test connection is successful, OIDC authentication is enabled.

### Configure a universe

To access a universe via OIDC, you need to set the following flags on the universe:

- ysql_hba_conf_csv
- ysql_ident_conf_csv

When the flags are set, YugabyteDB configures the `ysql_hba.conf` and `yb_ident.conf` files on the database nodes and creates the files that hold the JWKS keys for token validation.

For information on configuring flags in YugabyteDB Anywhere, refer to [Edit configuration flags](../../../manage-deployments/edit-config-flags/).

#### ysql_hba_conf_csv

The `ysql_hba_conf_csv` flag must be set to support using JWTs for authentication. The parameters to include in the configuration file record are as follows:

- `jwt_map` - the user-name map used to translate claim values to database roles. Optional if you aren't using the default Subject claim values.
- `jwt_issuers` - the first part of the discovery URL (`https://oauth.id.jumpcloud.com/`)
- `jwt_audiences` - the audience or target app for the token, which in this case is the client ID of the application you registered.
- `jwt_matching_claim_key` - the email attribute you set (for example, `preferred_username`). Optional if you aren't using the default Subject claim values.
- `jwt_jwks_path` - The JSON Web Key Set (JWKS) is a set of keys containing the public keys used to verify any JWT. These can be uploaded as entries in a single file. When configuring the flag in YugabyteDB Anywhere, click **Add JSON web key set (JWKS)** to upload the JWKS.

The following illustration shows an example of setting the `ysql_hba_conf_csv` flag in YugabyteDB Anywhere:

![Configuring ysql_hba_conf_csv flag for OIDC](/images/yp/security/oidc-jumpcloud-hbaconf.png)

The following shows an example `ysql_hba_conf_csv` flag configuration for OIDC:

```sh
host all all 0.0.0.0/0 jwt_map=map1 jwt_audiences=""<client_id>"" jwt_issuers=""https://oauth.id.jumpcloud.com/"" jwt_matching_claim_key=""preferred_username""
```

For more information on host authentication in YugabyteDB using `ysql_hba_conf_csv`, refer to [Host-based authentication](../../../../secure/authentication/host-based-authentication/).

#### ysql_ident_conf_csv

This flag is used to add translation regex rules that map token claim values to PostgreSQL roles. The flag settings are used as records in the `yb_ident.conf` file as user-name maps. This file is used identically to `pg_ident.conf` to map external identities to database users. For more information, refer to [User name maps](https://www.postgresql.org/docs/11/auth-username-maps.html) in the PostgreSQL documentation.

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

After OIDC-based authentication is configured, an administrator can manage users as follows:

- In the universe, add database users or roles.

  You need to add the users and roles that will be used to authenticate to the database. The role must be assigned the appropriate permissions in advance. Users will use their database user/role as their username credential along with their JWT as the password when connecting to the universe.

  For information on managing users and roles in YugabyteDB, see [Manage users and roles](../../../../secure/authorization/create-roles/).

- In YugabyteDB Anywhere, create YBA users.

  Create a user in YugabyteDB Anywhere for each user who wishes to sign in to YBA to obtain their JWT.

  To view their JWT, YBA users can sign in to YugabyteDB Anywhere, click the **User** icon at the top right, select **User Profile**, and click **Fetch OIDC Token**.

  This is not required if you enabled the **Display JWT token on login** option in the YBA OIDC configuration, as any database user can copy the JWT from the YBA landing page without signing in to YBA.

  For information on how to add YBA users, see [Create, modify, and delete users](../../../administer-yugabyte-platform/anywhere-rbac/#create-modify-and-delete-users).
