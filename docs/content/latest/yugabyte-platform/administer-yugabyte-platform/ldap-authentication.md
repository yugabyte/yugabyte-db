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

## Set up LDAP configuration

Configuring the LDAP server and defining requirements based on which it would work with Yugabyte Platform includes the following:

- Specifying the LDAP server endpoint.

- Specifying the LDAP port.

- Specifying the base directory name (DN) to enable restricting of users and user groups.

- Specifying your universally unique identifier (UUID). If not specified and it happens to be multi-tenant, then single-tenant is assumed by Yugabyte Platform.

- Defining the Yugabyte Platform-specific role in the LDAP server (`YugabytePlatformRole` annotation). If not specified, a default role which you can change at a later time, is assigned.

The configuration can be specified statically in the `application.conf` file or at runtime. New users can be created dynamically based on the LDAP server request. 

Since Yugabyte Platform and the LDAP server are synchronized during login, Yugabyte Platform always uses the up-to-date credentials and roles information, such as role and password changes, as well as removal of users deleted in the LDAP server.

If configured by the LDAP server, Yugabyte Platform can prevent the user from being able to change their password.

### Static configuration

You can define the Yugabyte Platform configuration for the LDAP server statically by setting the following parameters in the`application.conf` file:

```properties
security.use_ldap = true
security.ldap_url = "0.0.0.0"
security.ldap_port = 5099
security.ldap_BaseDN = ""
security.ldap_customeruuid = ""
```

- *security.use_ldap* - Boolean value to indicate whether or not to use LDAP for authentication.
- *security.ldap_url* - String value that defines the end-point of the LDAP URL.
- *security.ldap_port* - Integer value that defines the port to access LDAP server.
- *security.ldap_BaseDN* - String value that defines the base DN (if any) to be specified while querying the LDAP server.
- *security.ldap_customeruuid* - String value that defines the customer UUID. You should set this value if you or your organization want to use a preset existing UUID for the users. If there is no such intent, you can leave the string empty and Yugabyte Platform will generate a dynamic temporary customer UUID.

### Role annotation

*YugabytePlatformRole* - This is a requirement on the LDAP server side. Below shown is the way the parse a YugabytePlatformRole from the LDAP server.

