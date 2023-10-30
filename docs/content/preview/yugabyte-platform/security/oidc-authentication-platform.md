---
title: OIDC authentication in YugabyteDB Anywhere
headerTitle: OIDC authentication
linkTitle: OIDC authentication
description: Configuring YugabyteDB Anywhere to use an external OIDC authentication service.
headcontent: Manage database users using OIDC
menu:
  preview_yugabyte-platform:
    identifier: oidc-authentication-platform
    parent: security
    weight: 25
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../oidc-authentication-platform/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>

OpenID Connect (OIDC) is an authentication protocol that allows client applications to confirm the user's identity via authentication by an authorization server.

YugabyteDB supports authentication based on the OIDC protocol for access to YugabyteDB databases. This includes support for fine-grained access control using OIDC token claims and improved isolation with tenant-specific token signing keys.

You enable OIDC authentication in a YugabyteDB cluster by setting the OIDC configuration with the <code>[--ysql_hba_conf_csv](../../../reference/configuration/yb-tserver/#ysql-hba-conf-csv)</code> flag.

For information on using OIDC for authentication for YugabyteDB Anywhere, refer to [Enable YugabyteDB Anywhere authentication via OIDC](../../administer-yugabyte-platform/oidc-authentication/).

Note that the yugabyte privileged user will continue to exist as a local database user even after OIDC-based authentication is enabled for a universe.

## OIDC using Azure AD

This section describes how to configure a YugabyteDB Anywhere universe to use OIDC-based authentication for YugabyteDB database access with Azure AD and Entra ID as the Identity Provider (IdP).

There are 3 components that must be configured to enable OIDC authentication with Azure AD:

- Azure AD (AAD) /Entra ID - The Azure AD IdP configuration includes application registration (registering YugabyteDB in the AAD tenant) and configuring AAD to send tokens with the required claims to YugabyteDB.
- Yugabyte Anywhere - You can configure YBA to display the user's JSON Web Token (JWT) as well as configure authentication rules for database access using flags.
- YugabyteDB - The database is implicitly configured and will pick up on authentication rules set in YugabyteDB Anywhere. The database uses well-known PostgreSQL constructs to translate these authentication rules into database roles for access. Mapping Azure AD attributes, such as group memberships, roles, and email addresses to database roles, is accomplished using the PostgreSQL `yb_hba_conf` and `yb_ident_conf` files.

The Admin flow section details the steps that need to be followed by an access control admin to enable a user, and allow that user to fetch the JWT for database access. The User flow section has information about how to fetch the JWT and use it for database access.

### Configure Azure AD/Entra ID

By default, the Subject claim is used as the value to determine the role to assign to users for database access. In addition to the standard claims for token expiration, subject, and issuer, you have the option to use a non-standard claim (other than Subject) to determine role assignment. That is, the values of this claim will map the user to the database roles. This claim is denoted as `jwt_matching_claim_key`.

YugabyteDB expects the token created by the IdP to contain the following standard claims as well as the optional `jwt_matching_claim_key` to identify the end user and grant them the right access inside databases.

- Issuer
- Subject
- `jwt_matching_claim_key`, claim key, and values - these could be groups, roles, email, and so forth. Optionally, the subject claim can be used as the claim key.

The claims included in the token and chosen for user authorization will vary depending on your needs.

For example, to use group memberships as the determining factor for access and role assignment, you would include the groups claim in the initial token sent to the database. Note that the Subject claim can also be used to map the user to the PostgreSQL role.

These token claims are configured in the IdP's application registration.

The following illustration shows an example of Azure token configuration to ensure the right groups or roles claims are included in the token. Note that these options are available in higher tiers of Azure AD.

![Configuring the groups claims in Azure AD App registrations](/images/yp/security/oidc-azure-editgroup.png)

The following shows a example of a decoded JWT token with groups claims (the Group GUID is used in AAD OIDC tokens).

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

Note: GUIDs are not a supported format for YSQL usernames. Use regex rules in user name maps in `yp_ident_conf` to convert group GUIDs to roles as described in the following section.

OR

The following illustration shows an example of Azure app roles configuration.

![Configuring App Roles in Azure AD App registrations](/images/yp/security/oidc-azure-approles.png)

Decoded JWT token with app roles claims

The following shows a example of a decoded JWT token with app roles claims.

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

### Configure YugabyteDB Anywhere

You can configure OIDC-based database authentication using the Yugabyte Anywhere UI or API. OIDC settings are configured using flags.

For information on configuring flags in YugabyteDB Anywhere, refer to [Edit configuration flags](../../manage-deployments/edit-config-flags/).

#### ysql_hba_conf_csv

The `ysql_hba_conf_csv` flag can be configured to support using JWTs for authentication. The parameters to include in the configuration file record are as follows:

- jwt_issuers
- jwt_audiences - the audience or target app for the token.
- jwt_matching_claim_key - Optional if you aren't using the default Subject claim values.
- JWKS - The JSON Web Key Set (JWKS) is a set of keys containing the public keys used to verify any JWT. These can be uploaded as entries in a single file.
- Map - the user-name map used to translate claim values to database roles. Optional if you aren't using the default Subject claim values.

The following illustration shows an example flag configuration:

![Configuring ysql_hba_conf_csv flag for OIDC](/images/yp/security/oidc-azure-hbaconf.png)

The following shows an example flag configuration:

```sh
host all all 0.0.0.0/0 jwt map=map1 jwt_audiences=""<OIDC_CLIENT_ID>""   jwt_issuers=""https://login.microsoftonline.com/<AZURE_AD_TENANT_ID>/v2.0""  jwt_matching_claim_key=""preferred_username""
```

For more information on host authentication in YugabyteDB using ysql_hba_conf_csv, refer to [Host-based authentication](../../../secure/authentication/host-based-authentication/).

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

#### yb.runtime_conf_ui.tag_filter flag

[Tech Preview] Use the following API to set values for this flag:

```sh
curl -k --location --request PUT '<server-address>/api/v1/customers/<customerUUID>/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.runtime_conf_ui.tag_filter' \
--header 'Content-Type: text/plain' \
--header 'Accept: application/json' \
--header 'X-AUTH-YW-API-TOKEN: <api-token>' \
--data '["PUBLIC", "BETA"]'
```

#### yb.security.oidc_feature_enhancements configuration

This configuration setting must be enabled to expose the OIDC functionality in Yugabyte Anywhere. This flag shows when the `yb.runtime_conf_ui.tag_filter` flag is configured. To set this flag, navigate to **Admin > Advanced > Global Configuration.

![Configuring yb.security.oidc_feature_enhancements flag for OIDC](/images/yp/security/oidc-azure-globalfeature.png)

#### Display JWT token on login

You can control whether to allow the JWT to be displayed when OIDC authentication is enabled. When enabled, an option to show the user JWT is displayed on the Yugabyte Anywhere sign on page. This allows a user to view and copy their JWT without logging in to the YBA site.

To change this setting, navigate to **Admin > User Management > User Authentication > OIDC Configuration** and select the **Display JWT token on login** option.

### Yugabyte DB Configuration

YugabyteDB configures the `ysql_hba.conf` and `yb_ident.conf` files on the database nodes as well as creating the files that hold the JWKS keys for token validation.

### Manage users and roles

After OIDC-based authentication is configured, administrators can choose to add a convenient way for users to obtain their JWTs:

- Create YBA users

  Create a user in YugabyteDB Anywhere for each user who wishes to log in to the YBA site and obtain their JWT token. This is not needed for users who will copy the JWT from the YBA landing page without logging in.

- Add Database User or Role

  The user/role used to authenticate to the database must be pre-created by an admin for successful login. The role must be assigned the appropriate permissions in advance. The end user will use this database user/role as the username credential along with their JWT as the password.

- Displaying the user's JWT token

  To show users the option to view their JWT token in encoded form on the Yugabyte Anywhere landing page, enable the [Display JWT token on login](#display-jwt-token-on-login) setting.

Users who wish to log in to the portal can also view their JWTs on their User profile page.

### Using your JWT token

To view your JWT token, sign on to YugabyteDB Anywhere, click the **User** icon at the top right, and then select **User Profile**.

If the administrator has enabled the [Display JWT token on login](#display-jwt-token-on-login) setting, you can obtain your token from the Yugabyte Anywhere landing page. Click **Fetch JSON Web Token**; you are redirected to the IdP to enter your credentials as required. You are then redirected back to YugabyteDB Anywhere, where your JWT is displayed.

The JWT is displayed along with the expiration time of the token. The token must be used as the password to access the database before it expires. You can copy the JWT string to use as the password; the username to use will match the PG username/role that assigned to you by your administrator.
