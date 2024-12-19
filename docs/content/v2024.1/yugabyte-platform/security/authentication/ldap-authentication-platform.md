---
title: LDAP authentication for universes
headerTitle: LDAP authentication for universes
linkTitle: LDAP authentication
description: Configure YugabyteDB Anywhere to use LDAP authentication for databases.
headcontent: Manage database users using LDAP
menu:
  v2024.1_yugabyte-platform:
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

For more information, see [Edit configuration flags](../../../manage-deployments/edit-config-flags/).

When entering the flag value in YugabyteDB Anywhere, do not enclose it in single quotes, as you would in a Linux shell.

The first host-based authentication (HBA) rule `host all yugabyte 127.0.0.1/0 password` allows access to the admin user (yugabyte) from localhost (127.0.0.1) using password authentication. This allows the administrator to sign in as `yugabyte` to set up the roles and permissions for LDAP users.

The second HBA rule configures LDAP authentication for all other user-host pairs using a [search+bind](../../../../secure/authentication/ldap-authentication-ysql/#search-bind-mode) configuration. The YB-TServer binds to the LDAP directory using a fixed user name and password specified with `ldapbinddn` and `ldapbindpasswd`. The search is performed over the subtree at `ldapbasedn` and tries to find an exact match of the attribute specified in `ldapsearchattribute`.

After the user is found, to verify that the credentials are correct, the server disconnects and rebinds to the directory as this user using the password specified by the client.

For more information on the `ysql_hba_conf_csv` flag, refer to [--ysql_hba_conf_csv flag](../../../../reference/configuration/yb-tserver/#ysql-hba-conf-csv). For more information on HBA, refer to [Host-based authentication](../../../../secure/authentication/host-based-authentication/).

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
    ysqlsh (11.2-YB-2.5.1.0-b0)
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
    ysqlsh (11.2-YB-2.5.1.0-b0)
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
