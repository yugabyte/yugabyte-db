---
title: Enable Yugabyte Platform authentication via LDAP
headerTitle: Enable Yugabyte Platform authentication via LDAP
description: Use LDAP to enable login to Yugabyte Platform.
linkTitle: Authenticate with LDAP
aliases:
menu:
  latest:
    identifier: ldap-authentication
    parent: administer-yugabyte-platform
    weight: 20
isTocNested: true
showAsideToc: true
---

LDAP provides means for querying directory services. A directory typically stores credentials and permissions assigned to a user, therefore allowing to maintain a single repository of user information for all applications across the organization. In addition, having a hierarchical structure, LDAP allows creation of user groups requiring the same credentials.

LDAP authentication is similar to a direct password authentication, except that it employs the LDAP protocol to verify the password. This means that only users who already exist in the database and have appropriate permissions can be authenticated via LDAP. 

Yugabyte Platform's integration with LDAP enables you to use your LDAP server for authentication purposes instead of having to create user accounts on Yugabyte Platform.

Since Yugabyte Platform and the LDAP server are synchronized during login, Yugabyte Platform always uses the up-to-date credentials and roles information, such as role and password changes, as well as removal of users deleted in the LDAP server.

If configured by the LDAP server, Yugabyte Platform can prevent the user from being able to change their password.

## Use the Yugabyte Platform API

To enable LDAP authentication for Yugabyte Platform login, you need to use a number of API calls to specify the following:

- Your LDAP server endpoint, as follows:

  ```shell
  curl --location --request PUT 'https://10.9.140.199/api/v1/customers/f3a63f07-e3d6-4475-96e4-57a6453072e1/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.security.ldap.ldap_url' \
  --header 'X-AUTH-YW-API-TOKEN: 5182724b-1891-4cde-bcd1-b8f7a3b7331e' \
  --header 'Content-Type: text/plain' \
  --data-raw '10.9.140.199'
  ```

- LDAP usage, as follows:

  ```shell
  curl --location --request PUT 'https://10.9.140.199/api/v1/customers/f3a63f07-e3d6-4475-96e4-57a6453072e1/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.security.ldap.use_ldap' \
  --header 'X-AUTH-YW-API-TOKEN: 5182724b-1891-4cde-bcd1-b8f7a3b7331e' \
  --header 'Content-Type: text/plain' \
  --header 'Cookie: csrfCookie=d5cdb2b36b00fcad1f4fdb24605aee412f8dfaa0-1641544510767-641be933bf684abcade3c592' \
  --data-raw 'true'
  ```

- Thehe LDAP port, as follows:

  ```shell
  curl --location --request PUT 'https://10.9.140.199/api/v1/customers/f3a63f07-e3d6-4475-96e4-57a6453072e1/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.security.ldap.ldap_port' \
  --header 'X-AUTH-YW-API-TOKEN: 5182724b-1891-4cde-bcd1-b8f7a3b7331e' \
  --header 'Content-Type: text/plain' \
  --header 'Cookie: csrfCookie=d5cdb2b36b00fcad1f4fdb24605aee412f8dfaa0-1641544510767-641be933bf684abcade3c592' \
  --data-raw '389'
  ```

- The base directory name (DN) to enable restriction of users and user groups, as follows:

  ```shell
  curl --location --request PUT 'https://10.9.140.199/api/v1/customers/f3a63f07-e3d6-4475-96e4-57a6453072e1/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.security.ldap.ldap_basedn' \
  --header 'X-AUTH-YW-API-TOKEN: 5182724b-1891-4cde-bcd1-b8f7a3b7331e' \
  --header 'Content-Type: text/plain' \
  --header 'Cookie: csrfCookie=d5cdb2b36b00fcad1f4fdb24605aee412f8dfaa0-1641544510767-641be933bf684abcade3c592' \
  --data-raw '[LDAP DN]'
  ```

  <br>Replace `[LDAP DN]` with the actual value.

- The universally unique identifier (UUID) if you have a multi-tenant setup, as follows:

  ```shell
  curl --location --request PUT 'https://10.9.140.199/api/v1/customers/f3a63f07-e3d6-4475-96e4-57a6453072e1/runtime_config/00000000-0000-0000-0000-000000000000/key/yb.security.ldap.ldap_customeruuid' \
  --header 'X-AUTH-YW-API-TOKEN: 5182724b-1891-4cde-bcd1-b8f7a3b7331e' \
  --header 'Content-Type: text/plain' \
  --header 'Cookie: csrfCookie=d5cdb2b36b00fcad1f4fdb24605aee412f8dfaa0-1641544510767-641be933bf684abcade3c592' \
  --data-raw '[UUID]'
  ```

  <br>Replace `[UUID]` with the actual value.

  If the UUID is not specified, then single-tenant is assumed by Yugabyte Platform.

For additional information, see [Update a configuration key](https://yugabyte.stoplight.io/docs/yugabyte-platform/b3A6MTg5NDc2OTY-update-a-configuration-key).

## Define the Yugabyte Platform Role

You need to define a Yugabyte Platform-specific role for each user on your LDAP server by setting the a `YugabytePlatformRole` annotation.



