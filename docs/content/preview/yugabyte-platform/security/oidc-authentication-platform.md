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

You enable OIDC authentication in the YugabyteDB cluster by setting the OIDC configuration with the <code>[--ysql_hba_conf_csv](../../../reference/configuration/yb-tserver/#ysql-hba-conf-csv)</code> flag.

For information on using OIDC for authentication with YugabyteDB Anywhere, refer to [Enable YugabyteDB Anywhere authentication via OIDC](../../administer-yugabyte-platform/oidc-authentication/).

## OIDC using Azure AD

This section describes how to configure a YugabyteDB Anywhere universe to use OIDC-based authentication for YugabyteDB database access with Azure AD.

Yugabyte is introducing support for authentication based on the OIDC protocol for access to Yugabyte databases. This enhancement also includes support for fine-grained access control using OIDC token claims and improved isolation with tenant-specific token signing keys. Note that this runbook is intended for use with Azure AD as the Identity Provider (IdP).

There are 3 components that must be configured with the required settings to enable this authentication method:

- Azure AD (AAD) /Entra ID- The Azure AD IdP configuration includes app registration (registering Yugabyte in the AAD tenant) and configuring AAD to send tokens with the required claims to Yugabyte.
- Yugabyte Anywhere- The platform is configured to display the user’s JSON Web Token (JWT) as well as configure authentication rules for database access using flags.
- Yugabyte DB- The database is implicitly configured and will pick up on authentication rules set in Yugabyte Anywhere. The database will use well-known Postgres constructs to translate these authentication rules into database roles for access. The mapping of Azure AD attributes such as group memberships, or roles, or email addresses to database roles is accomplished using Postgres yb_hba_conf and yb_ident_conf files.


The sections that follow cover the details of the configurations for each of the components. The Admin flow section details the steps that need to be followed by an access control admin to enable a user, and allow that user to fetch the JWT for database access. The User flow section has information about how to fetch the JWT and use it for database access.

### Configure Azure AD/Entra ID

By default, the Subject claim will be used as the value to determine the role to assign to the user for database access.  In addition to the standard claims for token expiration, subject, and issuer, you have the option to use a non-standard claim (other than Subject)  to determine role assignment; i.e. the values of this claim will map the user to the database roles. This claim is denoted as “jwt_matching_claim_key”. Yugabyte expects the token created by the IdP to contain the following standard claims as well as the optional jwt_matching_claim_key  to identify the end user and help grant them the right access inside databases.

Issuer
Subject
Jwt_matching_claim_key, claim key and values- these could be “groups”, roles”, “email”, etc. Optionally, the subject claim can be used as the claim key.

The claims included in the token and chosen for authorization of a user may vary depending on the organization’s needs.

For example, to use group memberships as the determining factor for access and role assignment, then “groups” claim must be included in the initial token sent to the database. Note that the Subject claim can also be used to map the user to the PostgreSQL role.

These token claims are configured in the IdP's application registration.
Here is a sample of Azure app configuration that will ensure the right groups or roles claims are included in the token. Note that these options are available in higher tiers of Azure AD.

Configuring the groups claims in Azure AD App registrations

Decoded JWT token with groups claims (the Group GUID is used in AAD OIDC tokens).
Note: GUIDs are not a supported format for YSQL usernames. Use regex rules in user name maps in yp_ident_conf to convert group GUIDs to roles as described in the following section.

OR

Configuring App Roles in Azure AD App registrations

Decoded JWT token with app roles claims

Azure documentation- https://learn.microsoft.com/en-us/security/zero-trust/develop/configure-tokens-group-claims-app-roles

### Configure YugabyteDB Anywhere

Yugabyte Anywhere’s portal or APIs can be used to set up OIDC-based database authentication. There are a few separate controls available for organizations to customize the end user and administrator experience. These are described below.

Gflags
ysql_hba_conf_csv

Screenshot:

The existing ysql_hba_conf_csv has been enhanced to support using JWTs for authentication. The parameters to include in the hba_conf file record are as follows

jwt_issuers
jwt_audiences- the audience or target app for the token.
jwt_matching_claim_key- Optional if not using the default Subject claim values
JWKS- The JSON Web Key Set (JWKS) is a set of keys containing the public keys used to verify any JWT. These can be uploaded as entries in a single file via the Yugabyte Anywhere portal
Map- the user-name map used to translate claim values to database roles. Optional if not using the default Subject claim values

Sample flag configuration

`host all all 0.0.0.0/0 jwt map=map1 jwt_audiences=""<OIDC_CLIENT_ID>""   jwt_issuers=""https://login.microsoftonline.com/<AZURE_AD_TENANT_ID>/v2.0""  jwt_matching_claim_key=""preferred_username""`

ysql_ident_conf_csv

This new flag is used to add translation regex rules that map token claim values to Postgres roles. The flag settings are used as records in the yb_ident file as user-name maps. This file is used in an identical manner as pg_ident.conf to map external identities to database users.  

Screenshot:



Examples for the rules are shared below - 

Map a single user - 
map1 user@yugabyte.com user
Map multiple users 
	map2 /^(.*)@devadmincloudyugabyte\.onmicrosoft\.com$ \1
Map Roles <-> Users 
	map1 OIDC.Test.Read read_only_user

Runtime Flag

yb.runtime_conf_ui.tag_filter
This flag can be enabled with the BETA value ( ['PUBLIC', 'BETA']) to display private preview feature flags in the Yugabyte Anywhere UI. This flag is needed to show the fresh functionality for OIDC in Yugabyte Anywhere. Please use the following API to set values for this flag.
curl -k --location --request PUT '<server-address>/api/v1/customers/<customerUUID>/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.runtime_conf_ui.tag_filter' \
--header 'Content-Type: text/plain' \
--header 'Accept: application/json' \
--header 'X-AUTH-YW-API-TOKEN: <api-token>' \
--data '["PUBLIC", "BETA"]'

yb.security.oidc_feature_enhancements
This flag must be enabled to expose the OIDC functionality in Yugabyte Anywhere. This flag shows when the previously mentioned ‘yb.runtime_conf_ui.tag_filter’ flag is configured. To set this flag navigate to the Global Configuration tab under Admin-> Advanced.

Screenshot:



UI settings

The administrators have a control to show the user JWT. The control is a setting to allow the JWT to be displayed when OIDC authentication is enabled and when toggled on, an option to show the user JWT is displayed on the Yugabyte Anywhere landing page. This option allows a user to view and copy their JWT without logging in to the YBA site.
This toggle control under Admin-> User Management->User Authentication.

Screenshot:

### Yugabyte DB Configuration

Yugabyte will handle configuring the yb_hba_conf and yb_ident.conf files on the database nodes as well as creating the files that hold the JWKS keys for token validation.

Administrator Flow
After the previous configuration steps for setting the OIDC-based authentication method are completed, administrators can choose to add a convenient way for users to obtain their JWTs. The following sections describe how to configure this.

YBA user creation
In the first iteration of this feature, the administrator will create a Platform user in Yugabyte Anywhere for each user who wishes to log in to the YBA site and obtain their JWT token. This is not needed for users who will copy the JWT from the YBA landing page without logging in.

Database User or Role
The user/role used to authenticate to the database must be pre-created by an admin for successful login.The role must be assigned the appropriate permissions in advance. The end user will use this database user/role as the username credential along with their JWT as the password.

Displaying the user’s JWT token
To show users the option to view their JWT token in encoded form on the Yugabyte Anywhere landing page, enable the setting for  “Enable ‘Fetch JSON Web Token’ on login” on the OIDC Configurations tab under Admin-> User Management->User Authentication.

Users who wish to log in to the portal can also view their JWTs on their User profile pages. There is no separate control to be configured for this.

User Flow

An end user can view their JWT token from two locations. One is directly from the user’s profile page (after logging in to Yugabyte Anywhere) and the other is from the Yugabyte Anywhere landing page (after the administrator configures the controls to show the user JWT).The JWT will be displayed along with the expiration time of the token. The token must be used as the password to access the database before it expires.

If the setting to show the JSON on the landing page is set then the user will see a link “Fetch JSON Web Token” on the Yugabyte landing page. On clicking this link the user will be redirected as required to the IdP to enter their credentials as required. When the user is redirected to Yugabyte they will see their JWTs with the expiration of the token. The user can easily copy the JWT string to use as the password in the tooling of their choice. The username to use will match the PG username/role that the user wishes to assume.

As mentioned in the Admin flow section, the user/role used (used as the username) will have to pre-exist for successful database authentication.The role must be assigned the appropriate permissions in advance.

Considerations and notable points

The yugabyte privileged user will continue to exist as a local database user even after OIDC-based authentication is enabled for a cluster/Universe.
