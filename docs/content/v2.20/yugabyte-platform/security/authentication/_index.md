---
title: Configure database authentication in YugabyteDB Anywhere
headerTitle: Database authentication
linkTitle: Database authentication
headcontent: Secure your YugabyteDB universes
description: Database authentication in YugabyteDB Anywhere.
image: /images/section_icons/index/secure.png
menu:
  v2.20_yugabyte-platform:
    parent: security
    identifier: authentication
weight: 25
type: indexpage
---

YugabyteDB supports LDAP and OIDC for database authentication.

| Protocol | Description |
| :--- | :--- |
| LDAP | LDAP authentication is similar to password authentication, except that it uses the LDAP protocol to verify the database user's password. Before LDAP can be used for database authentication, users must already exist in the database and have appropriate permissions. |
| OIDC | OpenID Connect (OIDC) is an authentication protocol that allows client applications to confirm the user's identity via authentication by an authorization server. YugabyteDB supports authentication based on the OIDC protocol for access to YugabyteDB databases. This includes support for fine-grained access control using OIDC token claims and improved isolation with tenant-specific token signing keys. |

(For information on configuring user authentication for your YugabyteDB Anywhere instance, refer to [Configure authentication for YugabyteDB Anywhere](../../administer-yugabyte-platform/ldap-authentication/).)

{{<index/block>}}

  {{<index/item
    title="LDAP authentication"
    body="Use an external LDAP service to perform database client authentication."
    href="ldap-authentication-platform/"
    icon="/images/section_icons/secure/authentication.png">}}

  {{<index/item
    title="OIDC with Azure AD"
    body="Authenticate database users using SSO via Azure AD."
    href="oidc-authentication-aad/"
    icon="/images/section_icons/secure/authorization.png">}}

{{</index/block>}}
