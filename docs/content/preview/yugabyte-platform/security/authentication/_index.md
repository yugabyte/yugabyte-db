---
title: YugabyteDB Anywhere Authentication
headerTitle: Authentication
linkTitle: Authentication
headcontent: Secure your YugabyteDB universes
description: Authentication in YugabyteDB Anywhere.
image: /images/section_icons/index/secure.png
menu:
  preview_yugabyte-platform:
    parent: security
    identifier: authentication
weight: 25
type: indexpage
---

YugabyteDB supports LDAP and OIDC for authenticating with databases.

### LDAP

LDAP authentication is similar to password authentication, except that it uses the LDAP protocol to verify the password. Therefore, before LDAP can be used for authentication, the user must already exist in the database and have appropriate permissions.

For more information on LDAP in YugabyteDB, refer to [LDAP authentication](../../../secure/authentication/ldap-authentication-ysql/).

For information on using LDAP for authentication with YugabyteDB Anywhere, refer to [Enable YugabyteDB Anywhere authentication via LDAP](../../administer-yugabyte-platform/ldap-authentication/).

### OIDC

OpenID Connect (OIDC) is an authentication protocol that allows client applications to confirm the user's identity via authentication by an authorization server.

YugabyteDB supports authentication based on the OIDC protocol for access to YugabyteDB databases. This includes support for fine-grained access control using OIDC token claims and improved isolation with tenant-specific token signing keys.

For information on using OIDC for authentication for YugabyteDB Anywhere, refer to [Enable YugabyteDB Anywhere authentication via OIDC](../../administer-yugabyte-platform/oidc-authentication/).

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="ldap-authentication-platform/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/authentication.png" aria-hidden="true" />
        <div class="title">LDAP authentication</div>
      </div>
      <div class="body">
        Use an external LDAP service to perform client authentication.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="oidc-authentication-aad/">
      <div class="head">
        <img class="icon" src="/images/section_icons/secure/authorization.png" aria-hidden="true" />
        <div class="title">OIDC with Azure AD</div>
      </div>
      <div class="body">
        Authenticate database users using SSO via Azure AD.
      </div>
    </a>
  </div>

</div>
