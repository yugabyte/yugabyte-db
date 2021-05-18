---
title: LDAP Authentication
headerTitle: LDAP Authentication
linkTitle: LDAP Authentication
description: Configuring Yugabyte Platform to use an external LDAP authentication service.
menu:
  latest:
    identifier: ldap-authentication-platform
    parent: security
    weight: 25
isTocNested: false
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/latest/yugabyte-platform/security/ldap-authentication-platform/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>


The [LDAP Authentication](../../../secure/authentication/ldap-authentication/) method is similar to the password method, except that it uses LDAP to verify the password. Therefore, before LDAP can be used for authentication, the user must already exist in the database (and have appropriate permissions).

This section illustrates how to configure Yugabyte Platform to use an LDAP server such as Active Directory with TLS.


## TLS

Set the following
[TServer flag](../../../yugabyte-platform/manage-deployments/edit-config-flags/)
to use the `ldaptls=1` option to bind to the LDAP server using TLS:

  * **flag name**: `ysql_hba_conf_csv`
  * **flag value**:

  ```
  host all yugabyte 127.0.0.1/0 password,"host all all 0.0.0.0/0 ldap ldapserver=ldapserver.example.org ldapbasedn=""dc=example,dc=org"" ldapsearchattribute=uid ldapbinddn=""cn=admin,dc=example,dc=org"" ldapbindpasswd=secret ldaptls=1"
  ```

Note: when entering the above flag value into the GUI, it _does not_ have single quotes enclosing it like it would in a Linux shell.


The first HBA rule `host all yugabyte 127.0.0.1/0 password` allows access from the localhost (127.0.0.1) to the admin user (yugabyte) with password authentication.
This allows the administrator to login with the yugabyte user for setting up the roles (and permissions) for the LDAP users.

The second HBA rule configures LDAP authentication for all other user/host pairs
using a [search+bind](../../../secure/authentication/ldap-authentication/#search-bind-mode) configuration.
The YB-Tserver will bind to the LDAP directory with a fixed username and password specified with `ldapbinddn` and `ldapbindpasswd`.
The search will be performed over the subtree at `ldapbasedn` and will try to do an exact match of the attribute specified in `ldapsearchattribute`. Once the user has been found in this search, the server disconnects and re-binds to the directory as this user, using the password specified by the client, to verify that the login is correct.

For more information, see 
the [--ysql_hba_conf_csv](../../..//reference/configuration/yb-tserver/#ysql-hba-conf-csv) option
and [Host-based authentication](../../../secure/authentication/host-based-authentication).



## Example

1. Configure Yugabyte Platform with the `ysql_hba_conf_csv` flag as outlined in the previous section.

1. Create a user in Active Directory, and validate a successful search for that user.

    ```
    ldapsearch -x -H ldaps://ldapserver.example.org -b dc=example,dc=org 'uid=adam' -D "cn=admin,dc=example,dc=org" -w adminpassword
    ```

    <br>
    You should see a response similar to the following:

    ```
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

    If instead you see something like `ldap_sasl_bind(SIMPLE): Can't contact LDAP server (-1)` then you have a network, certificate, or binding (authentication) problem.

1. Create the user in YugabyteDB.

    ```
    % ysqlsh exampledb
    Password for user yugabyte: yugabyte
    ysqlsh (11.2-YB-2.5.1.0-b0)
    Type "help" for help.

    exampledb=# create user adam;
    CREATE ROLE
    ```

1. Validate the user can authenticate to the database using LDAP.

    ```
    % ysqlsh -U adam exampledb
    Password for user adam: supersecret
    ysqlsh (11.2-YB-2.5.1.0-b0)
    Type "help" for help.

    exampledb=> \conninfo
    You are connected to database "exampledb" as user "adam" on host "localhost" at port "5433".
    ```

