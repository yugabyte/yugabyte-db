---
title: Enable YugabyteDB Anywhere SSO authentication via OIDC
headerTitle: Configure authentication for YugabyteDB Anywhere
description: Use OIDC to enable SSO for YugabyteDB Anywhere.
headcontent: Manage YugabyteDB Anywhere users using OIDC
linkTitle: Configure authentication
menu:
  v2025.1_yugabyte-platform:
    identifier: oidc-authentication
    parent: administer-yugabyte-platform
    weight: 20
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../ldap-authentication/" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      LDAP
    </a>
  </li>
  <li >
    <a href="../oidc-authentication/" class="nav-link active">
      <i class="fa-solid fa-cubes"></i>
      OIDC
    </a>
  </li>
</ul>

You can configure YugabyteDB Anywhere to use OpenID Connect (OIDC) for single sign-on (SSO) authentication to access to your YugabyteDB Anywhere instance.

OIDC is an authentication protocol that allows client applications to confirm the user's identity via authentication by an authorization server.

When OIDC is enabled, users are presented with the following options when signing in to YugabyteDB Anywhere:

- **Login with SSO**: Redirects users to the appropriate identity provider sign in mechanism.
- **Local User Login**: User signs in to YugabyteDB Anywhere as a local user. You can restrict local user login to Super Admin only by setting the **Allow local login with SSO** Global Runtime Configuration option (config key `yb.security.allow_local_login_with_sso`) to false. Refer to [Manage runtime configuration settings](../../../yugabyte-platform/administer-yugabyte-platform/manage-runtime-config/).

Note that in versions earlier than v2025.1.3.0, only a Super Admin can sign in locally while OIDC is enabled.

To configure YugabyteDB Anywhere for OIDC, or to set global runtime configuration parameters, you need to be signed in as a Super Admin.

**Learn more**

- For information on configuring a YugabyteDB Anywhere universe to use OIDC-based authentication, refer to [OIDC authentication](../../security/authentication/oidc-authentication-aad/).

- For information on how to add users, see [Create, modify, and delete users](../anywhere-rbac/#users-and-roles). The email ID that you enter in the **Add User** dialog must be registered with the identity provider, and the role must reflect the user's role on YugabyteDB Anywhere.

## Use OIDC groups with YugabyteDB Anywhere roles

If your OIDC provider is configured with user groups, you can map the groups to [YugabyteDB Anywhere roles](../anywhere-rbac/). Users who are members of these groups can then sign in to YugabyteDB Anywhere without needing to be added to YugabyteDB Anywhere first. Users who are members of multiple groups are assigned the most privileged role.

Note that, if you use group mapping, you must manage users via your OIDC server. You can't add or change user roles in YugabyteDB Anywhere. In addition, group mapping overrides any previously assigned roles.

### Prerequisites

To use OIDC groups, ensure the following on your identity provider (IdP):

- Create user groups and add users to this group. This is possible on most IdPs.
- Configure the IdP so that groups are present in the ID token. As groups is not one of the [Standard Claims](https://openid.net/specs/openid-connect-core-1_0.html#StandardClaims), you will need to add the groups claim in the ID token by configuring your IdP provider settings. Refer to your IdP documentation.
- For Azure AD/Microsoft Entra ID, Azure doesn't allow obtaining group names in ID tokens. You need to use the [Azure API](https://learn.microsoft.com/en-gb/graph/api/user-list-memberof?view=graph-rest-1.0&tabs=http) to get a list of the user's group memberships. Note that to fetch the group membership via Azure API, the IdP administrator will need to assign the GroupMember.Read.All API permission to the registered application on Azure.

## Enable OIDC for YugabyteDB Anywhere

YugabyteDB Anywhere accepts OIDC configuration either using a discovery URL that points to the [OpenID Provider Configuration Document](https://openid.net/specs/openid-connect-discovery-1_0.html#ProviderConfig) for your provider, or by uploading the document directly. The configuration document contains key-value pairs with details about the OIDC provider's configuration, including uniform resource identifiers of the authorization, token, revocation, user information, and public-keys endpoints. YugabyteDB Anywhere uses the metadata to discover the URLs to use for authentication and the authentication service's public signing keys.

For air-gapped installations, where YugabyteDB Anywhere does not have access to the discovery URL, you need to explicitly provide the configuration document.

{{<tags/feature/ea idea="1501">}}You can map groups to [fine-grained](../anywhere-rbac/#fine-grained-rbac) YugabyteDB Anywhere roles. To enable the feature in YugabyteDB Anywhere, set the **Enable RBAC for Groups** Global Runtime Configuration option (config key `yb.security.group_mapping_rbac_support`) to true. Refer to [Manage runtime configuration settings](../../administer-yugabyte-platform/manage-runtime-config/). Note that only a Super Admin user can modify Global configuration settings.

You configure OIDC as follows:

1. Navigate to **Admin > User Management > User Authentication > OIDC Configuration**.

1. Select **OIDC Enabled** to turn on OIDC.

    ![OIDC authentication](/images/yp/oidc-auth-2024-2.png)

1. Complete the **OIDC Configuration** settings.

    - In the **Client ID** field, enter the unique identifier that you provided when you manually created the client application in the IdP.
    - In the **Client Secret** field, enter the password or secret for authenticating your Yugabyte client application with your IdP.
    - Use the **Discovery URL** field to provide a URL for the discovery document that contains OIDC configuration for the IdP. The discovery document is a JSON file stored in a well-known location.

        [Google OIDC discovery endpoint](https://developers.google.com/identity/protocols/oauth2/openid-connect#an-id-tokens-payload) is an example of such file. For most IdPs, `/.well-known/openid-configuration` is appended to the issuer to generate the metadata URL for OIDC specifications.

        If you have an airgapped installation, where YugabyteDB Anywhere cannot access the Discovery URL, provide the OIDC configuration for the IdP directly.

        To do this, click **Add OIDC Provider Configuration** and paste the OIDC configuration document from your IdP (in JSON format) into the field.

    - In the **Scope** field, enter your IdP OIDC scope that is allowed to be requested. This field accepts a space-separated list of values. If left blank, the defaults (`openid profile email`) will be considered.

      If you are mapping groups, add the name of the groups claim. For example, if your groups claim is called `groups`, you would set the scope to `openid profile email groups`.
    - In the **Email Attribute** field, enter the OIDC scope containing the user email identifier. This field accepts a case-sensitive custom configuration. Typically, this field is left blank.
    - If you have configured OIDC to use [refresh tokens](https://openid.net/specs/openid-connect-core-1_0.html#RefreshTokens), in the **Refresh Token URL** field, enter the URL of the refresh token endpoint.
    - If you have configured [OIDC enhancements](../../security/authentication/oidc-authentication-aad/#enable-oidc-enhancements), you can select the **Display JWT token on login** option to allow users to access their JWT from the YugabyteDB Anywhere sign in page. See [Set up OIDC with Azure AD on YugabyteDB Anywhere](../../security/authentication/oidc-authentication-aad/#set-up-oidc-with-azure-ad-on-yugabytedb-anywhere).

1. To map OIDC groups to YugabyteDB Anywhere roles, complete the **Role Settings**.

    - Select the **Use OIDC groups for authentication and authorization** option.

    - OIDC users who do not belong to any mapped group are assigned a default [built-in role](../anywhere-rbac/#built-in-roles), either **Read Only** or **Connect Only**.

    - Optionally, if you are using a groups claim, enter the name of the groups claim; this is the claim that lists the groups that users are a member of in the ID token.

1. Click **Save**.

### Map groups to roles

To map groups to roles, on the **Groups** tab, do the following:

1. Click **Add Group** and select **OIDC**.

1. Enter the Group DN name.

1. Select the YugabyteDB Anywhere role (built-in or custom) that you want to map the group to.

    - To assign a built-in role, on the **Built-in Role** tab, select a role. You can't assign the SuperAdmin role to a group.

    - To assign a custom role (only available if you have enabled RBAC for groups), on the **Custom Role** tab, select a role and scope.

1. Click **Save**.
