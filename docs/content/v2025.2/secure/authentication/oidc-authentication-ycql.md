---
title: OIDC authentication in YCQL
headerTitle: OIDC authentication in YCQL
linkTitle: OIDC authentication
description: Configure YugabyteDB YCQL to authenticate clients using JSON Web Tokens from an OpenID Connect identity provider.
menu:
  v2025.2:
    identifier: oidc-authentication-ycql
    parent: authentication
    weight: 733
type: docs
---
<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../oidc-authentication-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

OIDC authentication is similar to the [password](../password-authentication/) method, except that instead of a user-configured password, YugabyteDB uses the JSON Web Token (JWT) received from an external identity provider (IdP).

## Prerequisites

Before OIDC can be used for authentication:

- The user must already exist in the database (and have appropriate permissions).
- The external IdP must be configured.
- The [use_cassandra_authentication](../../../reference/configuration/yb-tserver/#use-cassandra-authentication) flag must set to `true`.
- To use OIDC authentication, set the YB-TServer flag `ycql_use_jwt_auth` to `true`.

OIDC for YCQL is available in v2025.2.4.0 and later.

## Configure OIDC

OIDC authentication for YCQL is configured by setting OIDC-related YB-TServer flags. Depending on your requirements, you can configure OIDC in two ways:

- _Without identity mapping_ between the IdP and the YCQL user.
- _With identity mapping_ between the IdP and the YCQL user (requires extra flags, described in [Identity mapping](#identity-mapping)).

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

## Identity mapping

Without identity mapping, the YCQL user name must match the IdP identity, that is, the YCQL user name must be the same as the identity of the user given by the `jwt_matching_claim_key` (that is, `JWT[jwt_matching_claim_key]`).

To allow different user names or to support group- or role-based authentication, configure an identity mapping between IdP identities and YCQL roles using the following flag:

| YB-TServer flag | Default | Description |
| :-------------- |-------- | :---------- |
| `ycql_ident_conf_csv` | empty | CSV formatted list of identity mapping rules, evaluated in order. Each rule contains two fields separated by whitespace: the IdP user name pattern and the YCQL user name. |

Identity mapping rules are similar to PostgreSQL [user name maps](https://www.postgresql.org/docs/15/auth-username-maps.html), where each rule has the form `idp-username database-username`. Separate rules using commas in the CSV flag value.

If `idp-username` starts with `/`, the remainder of the field is treated as a regular expression. The expression can contain a single capture group. The portion of the IdP user name that matched the capture can then be referenced in the database-username field as `\1` (backslash-one).

The following are examples of possible rules:

* Map a single user: `user@yugabyte.com user`
* Map multiple users with a domain pattern: `/^(.*)@devadmincloudyugabyte\.onmicrosoft\.com$ \1`
* Map a role name to a database role: `OIDC.Test.Read read_only_user`

## Examples

### Without identity mapping (username-based)

To use OIDC authentication on a new universe without identity mapping:

1. Start the YugabyteDB cluster with OIDC-related flags.

    For example, with Microsoft Entra ID (Azure AD), use the following command to start a YugabyteDB universe:

    ```sh
    ./bin/yugabyted start \
      --use_cassandra_authentication=true \
      --tserver_flags="ycql_use_jwt_auth=true,ycql_jwt_conf={jwt_jwks_url=https://login.microsoftonline.com/<tenant_id>/discovery/v2.0/keys jwt_audiences=<client_id> jwt_issuers=https://login.microsoftonline.com/<tenant_id>/v2.0 jwt_matching_claim_key=preferred_username},ycql_jwt_users_to_skip_csv=cassandra"
    ```

    The `ycql_jwt_users_to_skip_csv=cassandra` flag allows access to the user `cassandra` with password authentication. This allows the administrator to log in for setting up the roles (and permissions) for the OIDC users.

1. Start ycqlsh as `cassandra` and enter the default password when prompted.

    ```sh
    ./bin/ycqlsh -u cassandra
    ```

    ```output
    Connected to local cluster at 127.0.0.1:9042.
    [ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
    Use HELP for help.
    cassandra@ycqlsh>
    ```

1. Create a role for the OIDC user. The following example creates a role `johndoe` that matches the `preferred_username` claim.

    ```sql
    cassandra@ycqlsh> create role johndoe with login=true;
    ```

1. Connect to ycqlsh with OIDC authentication as the `johndoe` user and the JWT as the password (obtain the JWT from your IdP).

    ```sh
    ./bin/ycqlsh -u johndoe -p '<jwt>'
    ```

    If the JWT is valid and the user name is `johndoe`, authentication succeeds.

    ```output
    Connected to local cluster at 127.0.0.1:9042.
    [ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
    Use HELP for help.
    johndoe@ycqlsh>
    ```

### Identity mapping (role-based)

To use OIDC authentication with role-based usernames on a new universe, follow these steps:

1. Start the YugabyteDB cluster with OIDC-related flags and identity mapping.

    For example, the following command maps values such as `eng@company.com` to the YCQL user `eng` by stripping the domain:

    ```sh
    ./bin/yugabyted start \
      --use_cassandra_authentication=true \
      --tserver_flags="ycql_use_jwt_auth=true,ycql_jwt_conf={jwt_jwks_url=https://login.microsoftonline.com/<tenant_id>/discovery/v2.0/keys jwt_audiences=<client_id> jwt_issuers=https://login.microsoftonline.com/<tenant_id>/v2.0 jwt_matching_claim_key=roles},ycql_jwt_users_to_skip_csv=cassandra,ycql_ident_conf_csv={/^(.*)@company\.com$ \1}"
    ```

    With `jwt_matching_claim_key=roles`, YCQL reads identities from the `roles` claim. The `ycql_ident_conf_csv` rule maps each matching role string to the local part before `@company.com`, which must match an existing YCQL role name.

    The `ycql_jwt_users_to_skip_csv=cassandra` flag allows access to the user `cassandra` with password authentication. This allows the administrator to log in for setting up the roles (and permissions) for the OIDC users.

    The `ycql_ident_conf_csv={/^(.*)@company\.com$ \1}` flag configures a single identity mapping rule. It extracts the part before the `@company.com` from the IdP username (configured to be present in the `roles` claim) as the expected YCQL username.

1. Start ycqlsh as `cassandra` and enter the default password when prompted.

    ```sh
    ./bin/ycqlsh -u cassandra
    ```

    ```output
    Connected to local cluster at 127.0.0.1:9042.
    [ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
    Use HELP for help.
    cassandra@ycqlsh>
    ```

1. Create a YCQL role that corresponds to the mapped name. For example, a shared role `eng` for everyone who has the IdP role `eng@company.com`:

    ```sql
    cassandra@ycqlsh> create role eng with login=true;
    ```

1. Connect to ycqlsh with OIDC authentication using the mapped YCQL user name and the JWT as the password:

    ```sh
    ./ycqlsh -u eng -p '<jwt>'
    ```

    If the JWT is valid and the roles claim contains `eng@company.com`, authentication succeeds.

    ```output
    Connected to local cluster at 127.0.0.1:9042.
    [ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
    Use HELP for help.
    eng@ycqlsh>
    ```

## Related articles

- [OAuth 2.0 and OpenID Connect protocols](https://learn.microsoft.com/en-us/entra/identity-platform/v2-protocols) on Microsoft Learn
- [OIDC authentication with Azure AD in YugabyteDB Anywhere](../../../yugabyte-platform/security/authentication/oidc-authentication-aad/)
