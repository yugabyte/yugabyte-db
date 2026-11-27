---
title: LDAP authentication for universes
headerTitle: LDAP authentication for universes
linkTitle: LDAP authentication
description: Configure YugabyteDB Anywhere to use LDAP authentication for databases.
headcontent: Manage database users using LDAP
menu:
  v2025.2_yugabyte-platform:
    identifier: ldap-authentication-platform
    parent: authentication
    weight: 10
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../ldap-authentication-platform/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>

This section describes how to configure a YugabyteDB Anywhere universe to use an LDAP server such as Active Directory with TLS.

(For information on using LDAP to authenticate with YugabyteDB Anywhere, refer to [Enable YugabyteDB Anywhere authentication via LDAP](../../../administer-yugabyte-platform/ldap-authentication/).)

## Bind to the LDAP server using TLS

You enable LDAP authentication in the YugabyteDB universe by setting the LDAP configuration using the <code>[--ysql_hba_conf_csv](../../../../reference/configuration/yb-tserver/#ysql-hba-conf-csv)</code> flag.

To bind to the LDAP server using TLS, include the `ldaptls=1` option in the `ysql_hba_conf_csv` flag as per the following example:

```sh
host all yugabyte 127.0.0.1/0 password,"host all all 0.0.0.0/0 ldap ldapserver=ldapserver.example.org ldapbasedn=""dc=example,dc=org"" ldapsearchattribute=uid ldapbinddn=""cn=admin,dc=example,dc=org"" ldapbindpasswd=secret ldaptls=1"
```

If [API redaction of LDAP bind passwords](#redact-ldap-bind-passwords-in-api-responses) is enabled, YugabyteDB Anywhere displays the stored password as `ldapbindpasswd=REDACTED`. If you edit any part of the `ysql_hba_conf_csv` flag value, replace `REDACTED` with the actual password. If you don't change the value, you can leave `REDACTED` in place, and YugabyteDB Anywhere keeps the stored password.

For more information, see [Edit configuration flags](../../../manage-deployments/edit-config-flags/).

When entering the flag value in YugabyteDB Anywhere, do not enclose it in single quotes, as you would in a Linux shell.

The first host-based authentication (HBA) rule `host all yugabyte 127.0.0.1/0 password` allows access to the admin user (yugabyte) from localhost (127.0.0.1) using password authentication. This allows the administrator to sign in as `yugabyte` to set up the roles and permissions for LDAP users.

The second HBA rule configures LDAP authentication for all other user-host pairs using a [search+bind](../../../../secure/authentication/ldap-authentication-ysql/#search-bind-mode) configuration. The YB-TServer binds to the LDAP directory using a fixed user name and password specified with `ldapbinddn` and `ldapbindpasswd`. The search is performed over the subtree at `ldapbasedn` and tries to find an exact match of the attribute specified in `ldapsearchattribute`.

After the user is found, to verify that the credentials are correct, the server disconnects and rebinds to the directory as this user using the password specified by the client.

For more information on the `ysql_hba_conf_csv` flag, refer to [--ysql_hba_conf_csv flag](../../../../reference/configuration/yb-tserver/#ysql-hba-conf-csv). For more information on HBA, refer to [Host-based authentication](../../../../secure/authentication/host-based-authentication/).

## Redact LDAP bind passwords in API responses

In v2025.2.9.1 and later, you can configure YugabyteDB Anywhere to redact LDAP bind passwords in v1 and v2 API responses. When redaction is enabled, YugabyteDB Anywhere replaces the following values with `REDACTED`:

- The value of the `ycql_ldap_bind_passwd` flag.
- The value of each `ldapbindpasswd` option in the `ysql_hba_conf_csv` flag.

Redaction also applies to these values when they are set in Kubernetes universe overrides and AZ overrides.

YugabyteDB Anywhere doesn't redact other flags in API responses, such as `ycql_ldap_bind_dn` and the deprecated `ysql_hba_conf` flag.

YugabyteDB Anywhere always redacts LDAP bind passwords in logs, audit logs, and support bundles, independent of the API redaction setting.

### Enable API redaction

API redaction is disabled by default. To enable it, set the **Enable gflags sensitive data API redaction** Global Runtime Configuration option (config key `yb.api.enable_gflags_sensitive_data_redaction`) to true. You must be a Super Admin to set global runtime configuration options. Refer to [Manage runtime configuration settings](../../../administer-yugabyte-platform/manage-runtime-config/).

Although the option name refers to sensitive data in flags generally, it applies only to the LDAP bind password values listed in the preceding section.

When the option is enabled, YugabyteDB Anywhere does the following:

- Redacts LDAP bind passwords in API responses.
- Restores the stored password when a supported update request includes `REDACTED`.
- Rejects requests with HTTP 400 when it can't restore a `REDACTED` value.

Setting the option to false turns off all three behaviors.

Before you enable or disable the option, note the following:

- Update any automation that reads universe details from API responses and sends them back, such as the YugabyteDB Anywhere Terraform provider or custom API clients. These clients must handle `REDACTED` values and must not send them when creating a universe or when adding a cluster using the v1 API.
- Refresh the YugabyteDB Anywhere UI in your browser after changing the option. You don't need to restart YugabyteDB Anywhere. If you disable the option and then submit an **Edit Flags** form that was open before the change, YugabyteDB Anywhere can write the literal value `REDACTED` to the universe nodes.
- Don't use the string `REDACTED` as part of an actual password.

### Update flags with redaction enabled

When a supported update request includes a `REDACTED` value that came from an API response, YugabyteDB Anywhere replaces it with the stored password. This lets existing workflows that read and resubmit universe details keep working without exposing passwords.

Set or change any universe flags, including the LDAP flags, using only the following operations:

- Create universe
- Add read replica
- [Edit flags](../../../manage-deployments/edit-config-flags/)
- Resize node

Don't change flags using other universe edit operations.

YugabyteDB Anywhere handles `REDACTED` values as follows:

- **Editing `ysql_hba_conf_csv`**: YugabyteDB Anywhere compares the submitted value with the redacted form of the stored value. If they match, it restores the stored value. If you change any part of `ysql_hba_conf_csv` and leave `REDACTED` in place, YugabyteDB Anywhere rejects the request with HTTP 400. Provide the actual password whenever you modify this flag.
- **New universes and clusters**: A new cluster has no stored password to restore, so YugabyteDB Anywhere rejects `REDACTED` values with HTTP 400. When you add a read replica using the v2 API, or stop inheriting flags from the primary cluster on a read replica, YugabyteDB Anywhere restores `REDACTED` values from the flags stored for the primary cluster.
- **Validating Kubernetes overrides**: The `/validate_kubernetes_overrides` endpoint doesn't restore `REDACTED` values. If YugabyteDB Anywhere redacts the overrides as a single `REDACTED` value (for example, for complex YAML that it can't parse), use **Force Apply** or provide the full override text.
- **Stored `REDACTED` values**: If a universe already stores the literal value `REDACTED` for a password, YugabyteDB Anywhere can't recover the original value. Re-enter the actual passwords using [Edit flags](../../../manage-deployments/edit-config-flags/).

## Example

Consider the following example:

1. Configure YugabyteDB Anywhere with the `ysql_hba_conf_csv` flag, as outlined in [Bind to the LDAP server using TLS](#bind-to-the-ldap-server-using-tls).

1. Create a user in Active Directory and validate a successful search for that user, as follows:

    ```sh
    ldapsearch -x -H ldaps://ldapserver.example.org -b dc=example,dc=org 'uid=adam' -D "cn=admin,dc=example,dc=org" -w adminpassword
    ```

    You should see a response similar to the following:

    ```output
    # extended LDIF
    #
    # LDAPv3
    # base <dc=example,dc=org> with scope subtree
    # filter: uid=adam
    # requesting: ALL

    # adam, example.org
    dn: uid=adam,dc=example,dc=org
    objectClass: top
    objectClass: account
    objectClass: posixAccount
    objectClass: shadowAccount
    cn: adam
    uid: adam
    uidNumber: 16859
    gidNumber: 100
    homeDirectory: /home/adam
    loginShell: /bin/bash
    gecos: adam
    userPassword:: e2NyeXB0fXg=
    shadowLastChange: 0
    shadowMax: 0
    shadowWarning: 0

    # search result
    search: 2
    result: 0 Success

    # numResponses: 2
    # numEntries: 1
    ```

    If instead you see a message similar to `ldap_sasl_bind(SIMPLE): Can't contact LDAP server (-1)`, then you have a network, certificate, or binding (authentication) problem.  See [Troubleshoot LDAP issues](../../../troubleshoot/ldap-issues/).

1. Create the user in YugabyteDB, as follows:

    ```sh
    ysqlsh exampledb
    ```

    ```output
    Password for user yugabyte: yugabyte
    ysqlsh (15.2-YB-{{<yb-version version="v2025.2">}}-b0)
    Type "help" for help.
    ```

    ```sql
    exampledb=# CREATE USER adam;
    ```

1. Verify that the user can authenticate to the database using LDAP, as follows:

    ```sh
    ysqlsh -U adam exampledb
    ```

    ```output
    Password for user adam: supersecret
    ysqlsh (15.2-YB-{{<yb-version version="v2025.2">}}-b0)
    Type "help" for help.
    ```

    ```sql
    exampledb=# \conninfo
    ```

    Expect the following output:

    ```output
    You are connected to database "exampledb" as user "adam" on host "localhost" at port "5433".
    ```

## Learn more

For more information on LDAP in YugabyteDB, refer to [LDAP authentication](../../../../secure/authentication/ldap-authentication-ysql/).
