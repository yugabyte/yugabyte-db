---
title: LDAP Authentication in YSQL
headerTitle: LDAP Authentication in YSQL
linkTitle: LDAP Authentication
description: Configuring YugabyteDB to use an external LDAP authentication service.
menu:
  v2.8:
    identifier: ldap-authentication-1-ysql
    parent: authentication
    weight: 732
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../ldap-authentication-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="../ldap-authentication-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

The LDAP authentication method is similar to the password method, except that it uses LDAP to verify the password. Therefore, before LDAP can be used for authentication, the user must already exist in the database (and have appropriate permissions).

LDAP Authentication can be enabled in the YugabyteDB cluster by setting the LDAP configuration with the <code>[--ysql_hba_conf_csv](../../../reference/configuration/yb-tserver/#ysql-hba-conf-csv)</code> flag. YugabyteDB supports two modes for LDAP authentication:

* **simple-bind** mode
* **search+bind** mode

These are described below.

## Simple Bind Mode

In **simple-bind** mode, YB-TServer will bind to the Distinguished Name (“DN”) constructed with “prefix username suffix” format. Here is an example for Simple bind mode:

```sh
--ysql_hba_conf_csv='host all yugabyte 127.0.0.1/0 password,"host all all 0.0.0.0/0 ldap ldapserver=ldap.yugabyte.com ldapprefix=""uid="" ldapsuffix="", ou=DBAs, dc=example, dc=com"" ldapport=389"'
```

### Configurations

The configurations supported for simple bind mode.

| T-Server Gflag name | Default value | Description |
| :------------------ | :------------ | :---------- |
| `ldapserver` | (empty) | Names or IP addresses of LDAP servers to connect to. Separate servers with spaces. |
| `ldapport` | 389 | Port number on LDAP server to connect to. |
| `ldapscheme` | (empty) | Set to `ldaps` to use LDAPS. This is a non-standard way of using LDAP over SSL, supported by some LDAP server implementations. See also the `ldaptls` option for an alternative. |
| `ldaptls` | 0 | Set to 1 to make the connection between PostgreSQL and the LDAP server use TLS encryption. |
| `ldapprefix` | (empty) | String to be prepended to the user name when forming the DN for binding to the LDAP server. |
| `ldapsuffix` | (empty) | String to be appended to the user name when forming the DN for binding to the LDAP Server. |

## Search + Bind Mode

In `Search + Bind` mode, YB-Tserver will bind to the LDAP directory with a fixed username and password, specified with `ldapbinddn` and `ldapbindpasswd`, and performs a search for the user trying to log into the database. This mode is commonly used by LDAP authentication schemes in other software.

For Searching the LDAP directory if no fixed username and password is configured at YB-TServer, an anonymous bind will be attempted to the directory. The search will be performed over the subtree at `ldapbasedn`, and will try to do an exact match of the attribute specified in `ldapsearchattribute`. Once the user has been found in this search, the server disconnects and re-binds to the directory as this user, using the password specified by the client, to verify that the login is correct.

Here is an example for search + bind mode:

```sh
--ysql_hba_conf_csv='host all yugabyte 127.0.0.1/0 password,"host all all 0.0.0.0/0  ldap ldapserver=ldap.yugabyte.com ldapbasedn=""dc=yugabyte, dc=com"" ldapsearchattribute=uid"'
```

### Configurations

The configurations supported for search + bind mode.

| T-Server Gflag name | Default value | Description |
| :------------------ | :------------ | :---------- |
| `ldapserver` | (empty) | Names or IP addresses of LDAP servers to connect to. Separate servers with spaces. |
| `ldapport` | 389 | Port number on LDAP server to connect to. |
| `ldapscheme` | (empty) | Set to `ldaps` to use LDAPS. This is a non-standard way of using LDAP over SSL, supported by some LDAP server implementations. See also the `ldaptls` option for an alternative. |
| `ldaptls` | 0 | Set to 1 to make the connection between PostgreSQL and the LDAP server use TLS encryption. |
| `ldapbasedn` | (empty) | Specifies the base directory to begin the user name search |
| `ldapbinddn` | (empty) | Specifies the username to perform the initial search when doing search + bind authentication |
| `ldapbindpasswd` | (empty) | Password for username being used to perform the initial search when doing search + bind authentication |
| `ldapsearchattribute` | `uid` | Attribute to match against the username in the search when doing search + bind authentication. If no attribute is specified, the **uid** attribute is used. |
| `ldapsearchfilter` | (empty) | The search filter to use when doing search + bind authentication. |
| `ldapurl` | (empty) | An RFC 4516 LDAP URL. This is an alternative way to write LDAP options in a more compact and standard form. |

## Example

To use LDAP password authentication on a new YugabyteDB cluster, follow these steps:

1. Use  `--ysql_hba_conf_csv` config flag to enable LDAP authentication on YB-TServer. Use the below configuration to start a YugabyteDB cluster.

    ```sh
    --ysql_hba_conf_csv='host all yugabyte 127.0.0.1/0 password,"host all all 0.0.0.0/0 ldap ldapserver=ldap.forumsys.com ldapprefix=""uid="" ldapsuffix="", dc=example, dc=com"" ldapport=389"'
    ```

    {{< note title="Note" >}}
In the above sample configuration, we are using an [online LDAP test server](https://www.forumsys.com/tutorials/integration-how-to/ldap/online-ldap-test-server/) for setting up the LDAP authentication with YugabyteDB.
    {{< /note >}}

    For convenience we use two host based authentication (HBA) rules.

    * The first HBA rule `host all yugabyte 127.0.0.1/0 password` allows access from the localhost (127.0.0.1) to the admin user (yugabyte) with password authentication. This allows the administrator to log in with the yugabyte user for setting up the roles (and permissions) for the LDAP users.
    * The second HBA rule configures LDAP authentication for all other user/host pairs. We use simple bind with a uid-based username (ldapprefix) and a suffix defining the domain component (dc).

1. Start the YugabyteDB cluster.

1. Open the YSQL shell (ysqlsh), specifying the `yugabyte` user and prompting for the password.

    ```sh
    $ ./ysqlsh -U yugabyte -W
    ```

    When prompted for the password, enter the yugabyte password (default is `yugabyte`). You should be able to log in and see a response similar to the following:

    ```output
    ysqlsh (11.2-YB-2.3.3.0-b0)
    Type "help" for help.

    yugabyte=#
    ```

1. To display the current values in the `ysql_hba.conf` file, run the following `SHOW` statement to get the file location:

    ```sql
    yugabyte=# SHOW hba_file;
    ```

    ```output
                         hba_file
    -------------------------------------------------------
     /Users/yugabyte/yugabyte-data/node-1/disk-1/pg_data/ysql_hba.conf
    (1 row)
    ```

    Now view the file. The `ysql_hba.conf` file should have the following configuration:

    ```output
    # This is an autogenerated file, do not edit manually!
    host all yugabyte 127.0.0.1/0 trust
    host all all      0.0.0.0/0  ldap ldapserver=ldap.forumsys.com ldapprefix="uid=" ldapsuffix=", dc=example, dc=com" ldapport=389
    ```

1. Configure database role(s) for the LDAP user(s).

    We are creating a `ROLE` for username riemann supported by the test LDAP server.

    ```sql
    yugabyte=# CREATE ROLE riemann WITH LOGIN;
    yugabyte=# GRANT ALL ON DATABASE yugabyte TO riemann;
    ```

1. Connect using LDAP authentication.

    Connect ysqlsh using the `riemann` LDAP user and password specified in the [Online LDAP Test Server](https://www.forumsys.com/tutorials/integration-how-to/ldap/online-ldap-test-server/) page.

    ```sh
    $ ./ysqlsh -U riemann -W
    ```

    You can confirm the current user by running the following command:

    ```sql
    yugabyte=# SELECT current_user;
    ```

    ```output
     current_user
    --------------
     riemann
    (1 row)
    ```
