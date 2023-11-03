---
title: Enable Yugabyte Platform authentication via LDAP
headerTitle: Enable Yugabyte Platform authentication via LDAP
description: Use LDAP to enable log in to Yugabyte Platform.
linkTitle: Authenticate with LDAP
menu:
  v2.12_yugabyte-platform:
    identifier: ldap-authentication
    parent: administer-yugabyte-platform
    weight: 20
type: docs
---

LDAP provides means for querying directory services. A directory typically stores credentials and permissions assigned to a user, therefore allowing to maintain a single repository of user information for all applications across the organization. In addition, having a hierarchical structure, LDAP allows creation of user groups requiring the same credentials.

LDAP authentication is similar to a direct password authentication, except that it employs the LDAP protocol to verify the password. This means that only users who already exist in the database and have appropriate permissions can be authenticated via LDAP.

Yugabyte Platform's integration with LDAP enables you to use your LDAP server for authentication purposes instead of having to create user accounts on Yugabyte Platform.

Since Yugabyte Platform and the LDAP server are synchronized during login, Yugabyte Platform always uses the up-to-date credentials and roles information, such as role and password changes, as well as removal of users deleted in the LDAP server.

If configured by the LDAP server, Yugabyte Platform can prevent the user from being able to change their password.

## Use the Yugabyte Platform UI Console

You can use the Yugabyte Platform UI console to enable LDAP authentication for Yugabyte Platform login, as follows:

- Navigate to **Admin > User Management > User Authentication**.

- Complete the fields in the **LDAP Configuration** page shown in the following illustration:<br><br>

  ![LDAP authentication](/images/yp/ldap-auth-1.png)<br><br>

  With the exception of **LDAP URL**, the description of the preceding settings is provided in [Use the Yugabyte Platform API](#use-the-yugabyte-platform-ui). The **LDAP URL** field value represents a combination of the `ldap_url` and `ldap_port` values separated by a colon, as per the following example:<br>
  `0.0.0.0:9000`

## Use the Yugabyte Platform API

To enable LDAP authentication for Yugabyte Platform login, you can perform a number of runtime configurations to specify the following:

- LDAP usage `yb.security.ldap.use_ldap`, set to `true`, as follows:

  ```shell
  curl --location --request PUT 'https://10.9.140.199/api/v1/customers/f3a63f07-e3d6-4475-96e4-57a6453072e1/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.security.ldap.use_ldap' \
  --header 'X-AUTH-YW-API-TOKEN: 5182724b-1891-4cde-bcd1-b8f7a3b7331e' \
  --header 'Content-Type: text/plain' \
  --header 'Cookie: csrfCookie=d5cdb2b36b00fcad1f4fdb24605aee412f8dfaa0-1641544510767-641be933bf684abcade3c592' \
  --data-raw 'true'
  ```


- Your LDAP server endpoint `yb.security.ldap.ldap_url`, set using the *0.0.0.0* format, as follows:

  ```shell
  curl --location --request PUT 'https://10.9.140.199/api/v1/customers/f3a63f07-e3d6-4475-96e4-57a6453072e1/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.security.ldap.ldap_url' \
  --header 'X-AUTH-YW-API-TOKEN: 5182724b-1891-4cde-bcd1-b8f7a3b7331e' \
  --header 'Content-Type: text/plain' \
  --data-raw '10.9.140.199'
  ```

- The LDAP port `yb.security.ldap.ldap_port`, set using the *000* format, as follows:

  ```shell
  curl --location --request PUT 'https://10.9.140.199/api/v1/customers/f3a63f07-e3d6-4475-96e4-57a6453072e1/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.security.ldap.ldap_port' \
  --header 'X-AUTH-YW-API-TOKEN: 5182724b-1891-4cde-bcd1-b8f7a3b7331e' \
  --header 'Content-Type: text/plain' \
  --header 'Cookie: csrfCookie=d5cdb2b36b00fcad1f4fdb24605aee412f8dfaa0-1641544510767-641be933bf684abcade3c592' \
  --data-raw '389'
  ```

- The base distinguished name (DN) `yb.security.ldap.ldap_basedn` to enable restriction of users and user groups, as follows:

  ```shell
  curl --location --request PUT 'https://10.9.140.199/api/v1/customers/f3a63f07-e3d6-4475-96e4-57a6453072e1/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.security.ldap.ldap_basedn' \
  --header 'X-AUTH-YW-API-TOKEN: 5182724b-1891-4cde-bcd1-b8f7a3b7331e' \
  --header 'Content-Type: text/plain' \
  --header 'Cookie: csrfCookie=d5cdb2b36b00fcad1f4fdb24605aee412f8dfaa0-1641544510767-641be933bf684abcade3c592' \
  --data-raw '[LDAP DN]'
  ```

  <br>Replace `[LDAP DN]` with the actual value, as per the following example: <br>

  `,DC=yugabyte,DC=com`

- Prefix to the common name (CN) of the user `yb.security.ldap.ldap_dn_prefix`, as follows:

  ```shell
  curl --location --request PUT 'https://10.9.140.199/api/v1/customers/f3a63f07-e3d6-4475-96e4-57a6453072e1/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.security.ldap.ldap_dn_prefix' \
  --header 'X-AUTH-YW-API-TOKEN: 5182724b-1891-4cde-bcd1-b8f7a3b7331e' \
  --header 'Content-Type: text/plain' \
  --header 'Cookie: csrfCookie=d5cdb2b36b00fcad1f4fdb24605aee412f8dfaa0-1641544510767-641be933bf684abcade3c592' \
  --data-raw '[LDAP DN PREFIX]'
  ```

  <br>Replace `[LDAP DN PREFIX]` with the actual value, as per the following example: <br>

  `CN=`

  <br>Note that Yugabyte Platform combines `ldap_basedn` and `ldap_dn_prefix` with the username provided during login to query the LDAP server. `ldap_basedn` and `ldap_dn_prefix` should be set accordingly.

- The universally unique identifier (UUID) `yb.security.ldap.ldap_customeruuid`, if you have a multi-tenant setup, as follows:

  ```shell
  curl --location --request PUT 'https://10.9.140.199/api/v1/customers/f3a63f07-e3d6-4475-96e4-57a6453072e1/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.security.ldap.ldap_customeruuid' \
  --header 'X-AUTH-YW-API-TOKEN: 5182724b-1891-4cde-bcd1-b8f7a3b7331e' \
  --header 'Content-Type: text/plain' \
  --header 'Cookie: csrfCookie=d5cdb2b36b00fcad1f4fdb24605aee412f8dfaa0-1641544510767-641be933bf684abcade3c592' \
  --data-raw '[UUID]'
  ```

  <br>Replace `[UUID]` with the actual value.<br>

  If the UUID is not specified, then single-tenant is assumed by Yugabyte Platform.

- SSL usage with LDAP `yb.security.ldap.enable_ldaps`, set to `true` or `false` (default), as follows:

  ```shell
  curl --location --request PUT 'https://10.9.140.199/api/v1/customers/f3a63f07-e3d6-4475-96e4-57a6453072e1/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.security.ldap.enable_ldaps' \
  --header 'X-AUTH-YW-API-TOKEN: 5182724b-1891-4cde-bcd1-b8f7a3b7331e' \
  --header 'Content-Type: text/plain' \
  --header 'Cookie: csrfCookie=d5cdb2b36b00fcad1f4fdb24605aee412f8dfaa0-1641544510767-641be933bf684abcade3c592' \
  --data-raw 'true'
  ```

  <br>If the port is configured for an SSL connection, then you can enable SSL when you configure the LDAP server on Yugabyte Platform.

- TLS usage with LDAP `yb.security.ldap.enable_ldap_start_tls`, set to `true` or `false` (default), as follows:

  ```shell
  curl --location --request PUT 'https://10.9.140.199/api/v1/customers/f3a63f07-e3d6-4475-96e4-57a6453072e1/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.security.enable_ldap_start_tls' \
  --header 'X-AUTH-YW-API-TOKEN: 5182724b-1891-4cde-bcd1-b8f7a3b7331e' \
  --header 'Content-Type: text/plain' \
  --header 'Cookie: csrfCookie=d5cdb2b36b00fcad1f4fdb24605aee412f8dfaa0-1641544510767-641be933bf684abcade3c592' \
  --data-raw 'true'
  ```

  <br>If the port is configured for a TLS connection, then you can enable StartTLS when you configure the LDAP server on Yugabyte Platform.

  By default, if neither `ldap.enable_ldaps` or `ldap.enable_ldap_start_tls` is enabled, the connection will be unsecured.

When configured, Yugabyte Platform users are able to log in by specifying the common name of the user and the password to bind to the LDAP server.

For more information, see [Update a configuration key](https://yugabyte.stoplight.io/docs/yugabyte-platform/b3A6MTg5NDc2OTY-update-a-configuration-key).

In addition to the preceding parameters, you may choose to specify parameters for the service account credentials. This would be helpful in certain scenarios. For example, the Windows Active Directory (AD) server does not typically provide regular users with query permissions for the LDAP server. Setting service account credentials would enable these users to query the LDAP server ([the `yugabytePlatformRole` attribute](#define-the-yugabyte-platform-role) would be read and set accordingly). The service account should have enough permissions to query the LDAP server, find users, and read the user attributes.

The following are the runtime configurations to specify:

- A service account user name `yb.security.ldap.ldap_service_account_username`, as follows:

  ```shell
  curl --location --request PUT 'https://10.9.140.199/api/v1/customers/f3a63f07-e3d6-4475-96e4-57a6453072e1/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.security.ldap.ldap_service_account_username' \
  --header 'X-AUTH-YW-API-TOKEN: 5182724b-1891-4cde-bcd1-b8f7a3b7331e' \
  --header 'Content-Type: text/plain' \
  --header 'Cookie: csrfCookie=d5cdb2b36b00fcad1f4fdb24605aee412f8dfaa0-1641544510767-641be933bf684abcade3c592' \
  --data-raw '[SERVICE ACCOUNT USERNAME]'
  ```

  <br>Replace `[SERVICE ACCOUNT USERNAME]` with the actual value.<br>

- A service account password `yb.security.ldap.ldap_service_account_password`, as follows:

  ```shell
  curl --location --request PUT 'https://10.9.140.199/api/v1/customers/f3a63f07-e3d6-4475-96e4-57a6453072e1/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.security.ldap.ldap_service_account_password' \
  --header 'X-AUTH-YW-API-TOKEN: 5182724b-1891-4cde-bcd1-b8f7a3b7331e' \
  --header 'Content-Type: text/plain' \
  --header 'Cookie: csrfCookie=d5cdb2b36b00fcad1f4fdb24605aee412f8dfaa0-1641544510767-641be933bf684abcade3c592' \
  --data-raw '[SERVICE ACCOUNT PASSWORD]'
  ```

  <br>Replace `[SERVICE ACCOUNT PASSWORD]` with the actual value.<br>

## Define the Yugabyte Platform Role

You need to define a Yugabyte Platform-specific role for each user on your LDAP server by setting the `yugabytePlatformRole` annotation. The value set for this annotation is read during the Yugabyte Platform login. Note that if the value is modified on the LDAP server, the change is propagated to Yugabyte Platform and automatically updated during login. Password updates are also automatically handled.

If the role is not specified, users are created with ReadOnly privileges by default, which can be modified by the local super admin.

When LDAP is set up on a Windows Active Directory (AD) server, the user is expected to have permissions to query the user's properties from that server. If the permissions have not been granted, Yugabyte Platform defaults its role to ReadOnly, which can later be modified by the local super admin.
