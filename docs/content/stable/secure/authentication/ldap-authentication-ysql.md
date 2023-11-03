---
title: LDAP authentication in YSQL
headerTitle: LDAP authentication in YSQL
linkTitle: LDAP authentication
description: Configuring YugabyteDB to use an external LDAP authentication service using YSQL.
menu:
  stable:
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

LDAP Authentication can be enabled in the YugabyteDB cluster by setting the LDAP configuration with the [--ysql_hba_conf_csv](../../../reference/configuration/yb-tserver/#ysql-hba-conf-csv) flag.

YugabyteDB supports simple bind and search + bind modes for LDAP authentication.

## Simple bind mode

In simple bind mode, YB-TServer binds to the Distinguished Name ("DN") constructed with "prefix username suffix" format. The following is an example using simple bind mode:

```sh
--ysql_hba_conf_csv='host all yugabyte 127.0.0.1/0 password,"host all all 0.0.0.0/0 ldap ldapserver=ldap.yugabyte.com ldapprefix=""uid="" ldapsuffix="", ou=DBAs, dc=example, dc=com"" ldapport=389"'
```

### Configurations

The following configurations are supported for simple bind mode:

| T-Server flag | Default value | Description |
| :------------ | :------------ | :---------- |
| `ldapserver` | (empty) | Names or IP addresses of LDAP servers to connect to. Separate servers with spaces. |
| `ldapport` | 389 | Port number on LDAP server to connect to. |
| `ldapscheme` | (empty) | Set to `ldaps` to use LDAPS. This is a non-standard way of using LDAP over SSL, supported by some LDAP server implementations. See also the `ldaptls` option for an alternative. |
| `ldaptls` | 0 | Set to 1 to make the connection between PostgreSQL and the LDAP server use TLS encryption. |
| `ldapprefix` | (empty) | String to be prepended to the user name when forming the DN for binding to the LDAP server. |
| `ldapsuffix` | (empty) | String to be appended to the user name when forming the DN for binding to the LDAP Server. |

## Search + bind mode

In search + bind mode, YB-Tserver binds to the LDAP directory with a fixed username and password, specified with `ldapbinddn` and `ldapbindpasswd`, and performs a search for the user trying to log into the database. This mode is commonly used by LDAP authentication schemes in other software.

For searching the LDAP directory, if no fixed user name and password is configured on the YB-TServer, an anonymous bind will be attempted to the directory. The search is performed over the subtree at `ldapbasedn`, and tries to do an exact match of the attribute specified in `ldapsearchattribute`. When the user is found, the server disconnects and rebinds to the directory as this user via the password specified by the client to verify that the login is correct.

The following is an example of search + bind mode:

```sh
--ysql_hba_conf_csv='host all yugabyte 127.0.0.1/0 password,"host all all 0.0.0.0/0  ldap ldapserver=ldap.yugabyte.com ldapbasedn=""dc=yugabyte, dc=com"" ldapsearchattribute=uid"'
```

### Configurations

The following configurations are supported for search + bind mode:

| T-Server flag | Default value | Description |
| :------------ | :------------ | :---------- |
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

## Create secrets for Kubernetes

In Kubernetes, you can create secrets with sensitive information in the namespace where you are running YugabyteDB. Later, when creating universe pods, you can refer to those secrets in environment variables to use in configuring LDAP.

To create a secret:

1. Kubernetes expects secret data to be in base64 format. Run the following command in a shell to convert a password into base64 format:

    ```sh
    printf 'PASSWORD' | base64
    ```

    Replace PASSWORD with your password.

1. Add the following contents to a YAML file:

    ```yaml
    apiVersion: v1
    kind: Secret
    data:
      <KEY_NAME>: <Base64_password>
    metadata:
      annotations:
      name: <SECRET_NAME>
    type: Opaque
    ```

    Replace `Base64 password` with the base64 password you generated.

    Provide a key name and secret name.

1. Execute the following command to create the secret in the namespace running YugabyteDB:

    ```sh
    kubectl apply -n <namespace> -f <path-to-yaml-file>
    ```

    Replace namespace with the namespace running YugabyteDB. Replace `path-to-yaml-file` with the full path to the YAML file you created in the preceding step.

1. To use the secret for LDAP in a universe in YugabyteDB Anywhere, [create the provider](../../../yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/kubernetes/#overrides) and add the following override configuration:

    ```yml
    tserver:
      secretEnv:
      - name: YSQL_LDAP_BIND_PWD_ENV
        valueFrom:
          secretKeyRef:
            key: <KEY_NAME>
            name: <SECRET_NAME>
    ```

    Where `YSQL_LDAP_BIND_PWD_ENV` is the name of the environment variable assigned to the Kubernetes secret, and KEY_NAME and SECRET_NAME are the values you assigned when creating the secret.

    Any universe that you create with this provider can use the secret for the LDAP password. The secret can include multiple key-value pairs; only the specific key's value is passed as the LDAP password.

    Note that this provider only works if the namespace entered in the **Namespace** field is user-created; namespaces auto-generated by YugabyteDB Anywhere are not supported.

## Example configuration

To use LDAP password authentication on a new YugabyteDB cluster, follow these steps:

1. Start the YugabyteDB cluster using the `--ysql_hba_conf_csv` configuration flag to enable LDAP authentication on YB-TServer.

    Use the following configuration to start the cluster:

    ```sh
    --ysql_hba_conf_csv='host all yugabyte 127.0.0.1/0 password,"host all all 0.0.0.0/0 ldap ldapserver=ldap.forumsys.com ldapprefix=""uid="" ldapsuffix="", dc=example, dc=com"" ldapport=389"'
    ```

    {{< note title="Note" >}}
This sample configuration uses an [online LDAP test server](https://www.forumsys.com/tutorials/integration-how-to/ldap/online-ldap-test-server/) to set up the LDAP authentication with YugabyteDB.
    {{< /note >}}

    For convenience, the configuration uses two [host-based authentication](../host-based-authentication/) rules:

    * The first rule `host all yugabyte 127.0.0.1/0 password` allows access from localhost (127.0.0.1) to the admin user (`yugabyte`) with password authentication. This allows the administrator to log in to set up the roles (and permissions) for LDAP users.
    * The second rule configures LDAP authentication for all other user/host pairs, using simple bind with a uid-based username (`ldapprefix`) and a suffix defining the domain component (`dc`).

1. Start the YSQL shell (ysqlsh) specifying the `yugabyte` user, and enter the password (default is `yugabyte`) when prompted.

    ```sh
    $ ./ysqlsh -U yugabyte -W
    ```

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

1. View the file. The `ysql_hba.conf` file should have the following configuration:

    ```output
    # This is an autogenerated file, do not edit manually!
    host all yugabyte 127.0.0.1/0 trust
    host all all      0.0.0.0/0  ldap ldapserver=ldap.forumsys.com ldapprefix="uid=" ldapsuffix=", dc=example, dc=com" ldapport=389
    ```

1. Configure database role(s) for the LDAP user(s). The following commands create a `ROLE` for user `riemann` supported by the test LDAP server:

    ```sql
    yugabyte=# CREATE ROLE riemann WITH LOGIN;
    yugabyte=# GRANT ALL ON DATABASE yugabyte TO riemann;
    ```

1. Connect using LDAP authentication.

    Connect via ysqlsh using the `riemann` LDAP user and password specified in the [Online LDAP Test Server](https://www.forumsys.com/tutorials/integration-how-to/ldap/online-ldap-test-server/) page.

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
