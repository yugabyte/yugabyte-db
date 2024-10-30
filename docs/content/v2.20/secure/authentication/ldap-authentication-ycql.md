---
title: LDAP authentication in YCQL
headerTitle: LDAP authentication in YCQL
linkTitle: LDAP authentication
description: Configuring YugabyteDB to use an external LDAP authentication service using YCQL.
menu:
  v2.20:
    identifier: ldap-authentication-2-ycql
    parent: authentication
    weight: 732
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../ldap-authentication-ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="../ldap-authentication-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

The LDAP authentication method is similar to the password method, except that it uses LDAP to verify the password. Therefore, before LDAP can be used for authentication, the user must already exist in the database (and have appropriate permissions).

LDAP Authentication for YCQL can be enabled in the YugabyteDB cluster by setting the LDAP configuration with a set of TServer gflags. YugabyteDB supports two modes for LDAP authentication for YCQL (described in detail below):

* **simple-bind** mode
* **search+bind** mode

A prerequisite to using LDAP for YCQL is that the `use_cassandra_authentication` flag should be set to true. A set of configuration gflags common to both modes are -

| T-Server Gflag name | Default value | Description |
| :------------------ | :------------ | :---------- |
| `ycql_use_ldap` | false | Enable LDAP for YCQL |
| `ycql_ldap_users_to_skip_csv` | (empty) | Comma-separated list of users that are authenticated via the local password mechanism even if `ycql_use_ldap` is true. |
| `ycql_ldap_server` | (empty) | LDAP server endpoint of the form _scheme_://_ip_:_port_. Scheme can be `ldap` (or) `ldaps`. |
| `ycql_ldap_tls` | false | (Not yet supported) Connect to LDAP server using TLS encryption |

## Simple Bind Mode

In **simple-bind** mode, YB-TServer will bind to the Distinguished Name ("DN") constructed with "prefix username suffix" format. Here is an example for Simple bind mode:

```sh
--use_cassandra_authentication=true --ycql_use_ldap=true --ycql_ldap_server=ldap://ldap.yugabyte.com:389 --ycql_ldap_user_prefix=uid= --ycql_ldap_user_suffix=, ou=DBAs, dc=example, dc=com --ycql_ldap_users_to_skip_csv=cassandra
```

### Configurations

The configuration specific to simple bind mode.

| T-Server Gflag name | Default value | Description |
| :------------------ | :------------ | :---------- |
| `ycql_ldap_user_prefix` | (empty) | String to prepend to the user name when forming the DN for binding to the LDAP server |
| `ycql_ldap_user_suffix` | (empty) | String to append to the user name when forming the DN for binding to the LDAP server |

## Search + Bind Mode

In `Search + Bind` mode, YB-Tserver will bind to the LDAP directory with a fixed username and password, specified with `ycql_ldap_bind_dn` and `ycql_ldap_bind_passwd`, and performs a search for the user trying to log in to the database. This mode is commonly used by LDAP authentication schemes in other software.

For Searching the LDAP directory if no fixed username and password is configured at YB-TServer, an anonymous bind will be attempted to the directory. The search will be performed over the subtree at `ycql_ldap_base_dn`, and will try to do an exact match of the attribute specified in `ycql_ldap_search_attribute`. After the user has been found in this search, the server disconnects and re-binds to the directory as this user, using the password specified by the client, to verify that the login is correct.

Here is an example for search + bind mode:

```sh
--use_cassandra_authentication=true --ycql_use_ldap=true --ycql_ldap_server=ldap://ldap.yugabyte.com:389 --ycql_ldap_base_dn="dc=yugabyte, dc=com" --ycql_ldap_bind_dn="cn=admin,dc=example,dc=org" --ycql_ldap_bind_passwd=admin --ycql_ldap_search_attribute=uid
```

### Configurations

The configurations supported for search + bind mode.

| T-Server Gflag name | Default value | Description |
| :------------------ | :------------ | :---------- |
| `ycql_ldap_base_dn` | (empty) | Base directory to begin the user name search |
| `ycql_ldap_bind_dn` | (empty) | Username to perform the initial search when doing search + bind authentication |
| `ycql_ldap_bind_passwd` | (empty) | Password for the username being used to perform the initial search when doing search + bind authentication |
| `ycql_ldap_search_attribute` | `uid` attribute | Attribute to match against the username in the search when doing search + bind authentication. If no attribute is specified, the `uid` attribute is used. |
| `ycql_ldap_search_filter` | (empty) | Search filter to use when doing search + bind authentication |

## Example

To use LDAP password authentication on a new YugabyteDB cluster, follow these steps:

1. Use TServer gflags to enable LDAP authentication on YB-TServer in simple bind mode. Use the below configuration to start a YugabyteDB cluster.

    ```sh
    --use_cassandra_authentication=true --ycql_use_ldap=true --ycql_ldap_server=ldap://ldap.forumsys.com:389 --ycql_ldap_user_prefix=uid= --ycql_ldap_user_suffix=, dc=example, dc=com --ycql_ldap_users_to_skip_csv=cassandra
    ```

    {{< note title="Note" >}}
In the above sample configuration, we are using an [online LDAP test server](https://www.forumsys.com/tutorials/integration-how-to/ldap/online-ldap-test-server/) for setting up the LDAP authentication with YugabyteDB.
    {{< /note >}}

    The `--ycql_ldap_users_to_skip_csv=cassandra` gflag allows access to the user `cassandra` with password authentication. This allows the administrator to log in for setting up the roles (and permissions) for the LDAP users.

1. Start the YugabyteDB cluster.

1. Open the YCQL shell (ycqlsh), specifying the `cassandra` user and prompting for the password.

    ```sh
    $ ./ycqlsh -u cassandra
    ```

    When prompted for the password, enter `cassandra` (default password of `cassandra` user). You should be able to log in and see a response similar to the following:

    ```output
    Connected to local cluster at 127.0.0.1:9042.
    [ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
    Use HELP for help.
    cassandra@ycqlsh>
    ```

1. Configure database role(s) for the LDAP user(s).

    We are creating a `ROLE` for username `riemann` supported by the test LDAP server.

    ```sql
    cassandra@ycqlsh> create role riemann with login=true;
    ```

1. Connect using LDAP authentication.

    Connect ycqlsh using the `riemann` LDAP user and password specified in the [Online LDAP Test Server](https://www.forumsys.com/tutorials/integration-how-to/ldap/online-ldap-test-server/) page.

    ```sh
    $ ./ycqlsh -u riemann
    ```

    ```output
    Connected to local cluster at 127.0.0.1:9042.
    [ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
    Use HELP for help.
    riemann@ycqlsh>```
