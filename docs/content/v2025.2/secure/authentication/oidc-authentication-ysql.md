---
title: OIDC authentication in YSQL
headerTitle: OIDC authentication in YSQL
linkTitle: OIDC authentication
description: Configure YugabyteDB YSQL to authenticate clients using JSON Web Tokens from an OpenID Connect identity provider.
menu:
  v2025.2:
    identifier: oidc-authentication-ysql
    parent: authentication
    weight: 733
type: docs
---

{{<api-tabs>}}

OIDC authentication is similar to the [password](../password-authentication/) method, except that instead of a user-configured password, YugabyteDB uses the JSON Web Token (JWT) received from an external identity provider (IdP). Pass the JWT as the password when you connect.

On YugabyteDB Aeon, Yugabyte sets these flags for you on request. Contact {{% support-cloud %}}.

## Prerequisites

Before OIDC can be used for YSQL authentication:

- The role must already exist in the database and have the `LOGIN` privilege.
- The external IdP must be configured to issue tokens for your clients.
- You need the token issuer, the audience, the provider's JSON Web Key Set (JWKS), and the claim that identifies the user.

## Configure a JWT rule

Add a `jwt` rule to the YB-TServer [--ysql_hba_conf_csv](../../../reference/configuration/yb-tserver/#ysql-hba-conf-csv) flag, and set the same value on every YB-TServer. YugabyteDB writes the flag to `ysql_hba.conf`, checks the records from top to bottom, and uses the first record that matches the connection. If that authentication fails, later records are not tried. For the record format, see [Host-based authentication](../host-based-authentication/).

A `jwt` rule requires `jwt_audiences`, `jwt_issuers`, and either `jwt_jwks_url` or `jwt_jwks_path`.

| Option | Required | Description |
| :----- | :------- | :---------- |
| `jwt_jwks_url` | One of URL or path | URL of the provider's JWKS. YugabyteDB fetches this URL on each login, so a signing-key rotation takes effect on the next login, without a YB-TServer restart. |
| `jwt_jwks_path` | One of URL or path | Path of a file that contains the JWKS. Use this when the nodes cannot reach the provider. If you set both options, YugabyteDB uses `jwt_jwks_url`. |
| `jwt_issuers` | Yes | Comma-separated list of accepted issuers. A token is accepted only when its issuer claim equals one of these values. Match the issuer string exactly, including the scheme and a trailing slash when the provider sends one. For more information, see the [issuer claim](https://openid.net/specs/openid-connect-core-1_0.html#IDToken) in the OpenID Connect Core specification. |
| `jwt_audiences` | Yes | Comma-separated list of accepted audiences. A token is accepted only when its audience claim equals one of these values. For more information, see the [audience claim](https://openid.net/specs/openid-connect-core-1_0.html#IDToken) in the OpenID Connect Core specification. |
| `jwt_matching_claim_key` | No | Claim that carries the IdP identity (for example, `sub`, `email`, `groups`, or `roles`). The default is `sub`. |
| `map` | No | Name of an [identity map](#identity-mapping). If you omit `map`, the claim value must equal the database role name. |

If an option value contains a comma, quote the value in the record and quote that record in the CSV flag. See the [--ysql_hba_conf_csv](../../../reference/configuration/yb-tserver/#ysql-hba-conf-csv) flag for the quoting rules.

Use `hostssl` so the token is sent only over TLS. For connections without TLS, use `host` instead of `hostssl`. See [Connect to clusters](../../tls-encryption/connect-to-cluster/).

## Keep password authentication for selected roles

Because the first matching rule wins, put a password rule for each role that should keep using a database password above the `jwt` rule.

For example, let `yugabyte` sign in with a password, and require a JWT for every other role:

```text
hostssl all yugabyte all scram-sha-256
hostssl all all all jwt jwt_jwks_url=https://www.googleapis.com/oauth2/v3/certs jwt_issuers=https://accounts.google.com jwt_audiences=<audience> jwt_matching_claim_key=email
```

The password method (`scram-sha-256` or `md5`) must match the password stored for that role. See [Password authentication](../password-authentication/).

On YCQL, use [`ycql_jwt_users_to_skip_csv`](../oidc-authentication-ycql/#configure-oidc) for the same kind of exception.

## Identity mapping

If the `jwt` rule omits `map`, the value of `jwt_matching_claim_key` must equal the role name the client requests.

To allow a different role name, or to match one value from a group or role claim, set `map` on the `jwt` rule and add rules to the [--ysql_ident_conf_csv](../../../reference/configuration/yb-tserver/#ysql-ident-conf-csv) flag. Each rule has the form `map-name idp-identity database-role`. Separate rules with commas. The map name must be the same as the `map` option on the `jwt` rule. YugabyteDB evaluates the rules in order and accepts the login when one claim value matches a rule for the requested role.

If `idp-identity` starts with `/`, the remainder of the field is a regular expression. The expression can contain a single capture, which you reference in the database role as `\1` (backslash-one).

The following are examples of possible rules:

* Map one user: `oidc user@example.com app_user`
* Map a group or role claim: `oidc analysts analyst`
* Map a domain pattern: `oidc /^(.*)@example\.com$ \1`

```sh
--ysql_ident_conf_csv='oidc user@example.com app_user,oidc analysts analyst,oidc /^(.*)@example\.com$ \1'
```

Add `map=oidc` to the `jwt` rule when you use these rules. For more information, see [User name maps](https://www.postgresql.org/docs/15/auth-username-maps.html) in the PostgreSQL documentation.

## Google ID tokens

The following values apply to a Google service-account ID token:

| Setting | Value |
| :------ | :---- |
| Issuer (`jwt_issuers`) | `https://accounts.google.com` (no trailing slash) |
| JWKS (`jwt_jwks_url`) | `https://www.googleapis.com/oauth2/v3/certs` |
| Claim (`jwt_matching_claim_key`) | `email` |
| Audience (`jwt_audiences`) | The audience you requested when the token was created |

To try this provider on a new local cluster, without TLS:

1. Start a local cluster, set a password for `yugabyte`, and create a role whose name is the service account email. Quote the role name because it includes characters such as `@`.

    ```sh
    ./bin/yugabyted start
    ./bin/ysqlsh -c "ALTER ROLE yugabyte PASSWORD '<password>'"
    ./bin/ysqlsh -c 'CREATE ROLE "app@example.iam.gserviceaccount.com" LOGIN'
    ```

1. Restart the node with a password rule for `yugabyte` and a JWT rule for other roles. This example uses `host` rules because a local cluster started without `--secure` does not have TLS enabled, and a `hostssl` rule only matches TLS connections. For a cluster with TLS enabled, use `hostssl` instead.

    When you pass a CSV flag through yugabyted, wrap the value in curly braces. See [Pass additional flags to YB-Master and YB-TServer](../../../reference/configuration/yugabyted/#pass-additional-flags-to-yb-master-and-yb-tserver).

    ```sh
    ./bin/yugabyted stop
    ./bin/yugabyted start \
      --tserver_flags="ysql_hba_conf_csv={host all yugabyte all scram-sha-256,host all all all jwt jwt_jwks_url=https://www.googleapis.com/oauth2/v3/certs jwt_issuers=https://accounts.google.com jwt_audiences=<audience> jwt_matching_claim_key=email}"
    ```

1. Connect with the JWT as the password.

    ```sh
    PGPASSWORD='<jwt>' ./bin/ysqlsh -h <host> -U 'app@example.iam.gserviceaccount.com'
    ```

    The connection succeeds when the token signature, issuer, and audience match the rule, and the `email` claim equals the role name.

## Related articles

- [Host-based authentication](../host-based-authentication/)
- [OIDC authentication in YCQL](../oidc-authentication-ycql/)
- [OIDC database authentication in YugabyteDB Anywhere](../../../yugabyte-platform/security/authentication/oidc-database-authentication/)
