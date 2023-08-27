---
title: LDAP authentication
headerTitle: LDAP authentication
linkTitle: LDAP authentication
description: Configuring Yugabyte Platform to use an external LDAP authentication service.
menu:
  v2.12_yugabyte-platform:
    identifier: ldap-authentication-platform
    parent: security
    weight: 25
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

LDAP Authentication is similar to password authentication, except that it uses the LDAP protocol to verify the password. Therefore, before LDAP can be used for authentication, the user must already exist in the database and have appropriate permissions.

You enable LDAP authentication in the YugabyteDB cluster by setting the LDAP configuration with the <code>[--ysql_hba_conf_csv](../../../reference/configuration/yb-tserver/#ysql-hba-conf-csv)</code> flag.

This section describes how to configure Yugabyte Platform to use an LDAP server such as Active Directory with TLS.

For more information on LDAP in YugabyteDB, refer to [LDAP Authentication](../../../secure/authentication/ldap-authentication/).

## Bind to the LDAP server using TLS

To bind to the LDAP server using TLS, you set the `ldaptls=1` option. Set the `ysql_hba_conf_csv` flag to the following value:

```sh
host all yugabyte 127.0.0.1/0 password,"host all all 0.0.0.0/0 ldap ldapserver=ldapserver.example.org ldapbasedn=""dc=example,dc=org"" ldapsearchattribute=uid ldapbinddn=""cn=admin,dc=example,dc=org"" ldapbindpasswd=secret ldaptls=1"
```

Set the flag in Platform as described in [Edit configuration flags](../../../yugabyte-platform/manage-deployments/edit-config-flags/).

{{< note title="Note" >}}
When entering the flag value in Platform, do not enclose it in single quotes as you would in a Linux shell.
{{< /note >}}

The first host-based authentication (HBA) rule `host all yugabyte 127.0.0.1/0 password` allows access to the admin user (yugabyte) from localhost (127.0.0.1) using password authentication. This allows the administrator to log in as `yugabyte` to set up the roles (and permissions) for LDAP users.

The second HBA rule configures LDAP authentication for all other user/host pairs using a [search+bind](../../../secure/authentication/ldap-authentication/#search-bind-mode) configuration. The YB-TServer will bind to the LDAP directory using a fixed username and password specified with `ldapbinddn` and `ldapbindpasswd`. The search is performed over the subtree at `ldapbasedn` and tries to find an exact match of the attribute specified in `ldapsearchattribute`.

Once the user is found, to verify that the login is correct, the server disconnects and re-binds to the directory as this user using the password specified by the client.

For more information on the `ysql_hba_conf_csv` flag, refer to [--ysql_hba_conf_csv flag](../../../reference/configuration/yb-tserver/#ysql-hba-conf-csv). For more information on HBA, refer to [Host-based authentication](../../../secure/authentication/host-based-authentication).

## Example

1. Configure Yugabyte Platform with the `ysql_hba_conf_csv` flag as outlined in the previous section.

1. Create a user in Active Directory, and validate a successful search for that user.

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

    If instead you see something like `ldap_sasl_bind(SIMPLE): Can't contact LDAP server (-1)`, then you have a network, certificate, or binding (authentication) problem.

1. Create the user in YugabyteDB.

    ```sh
    % ysqlsh exampledb
    ```

    ```output
    Password for user yugabyte: yugabyte
    ysqlsh (11.2-YB-2.5.1.0-b0)
    Type "help" for help.
    ```

    ```sql
    exampledb=# CREATE USER adam;
    ```

1. Verify that the user can authenticate to the database using LDAP.

    ```sh
    % ysqlsh -U adam exampledb
    ```

    ```output
    Password for user adam: supersecret
    ysqlsh (11.2-YB-2.5.1.0-b0)
    Type "help" for help.
    ```

    ```sql
    exampledb=# \conninfo
    ```

    ```output
    You are connected to database "exampledb" as user "adam" on host "localhost" at port "5433".
    ```
