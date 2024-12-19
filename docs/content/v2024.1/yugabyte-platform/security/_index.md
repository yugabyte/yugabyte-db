---
title: YugabyteDB Anywhere Security
headerTitle: Security
linkTitle: Security
description: Secure YugabyteDB Anywhere and YugabyteDB universes.
headcontent: Secure YugabyteDB Anywhere and your YugabyteDB universes
menu:
  v2024.1_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: security
weight: 660
type: indexpage
---

You can apply security measures to protect your YugabyteDB Anywhere instance and YugabyteDB universes.

## Network security

You need to ensure that YugabyteDB Anywhere and the database run in a trusted network environment. You should restrict machine and port access, based on the following guidelines:

- Servers running YugabyteDB services are directly accessible only by YugabyteDB Anywhere, servers running the application, and database administrators.
- Only YugabyteDB Anywhere and servers running applications can connect to YugabyteDB services on the RPC ports. Access to the YugabyteDB ports should be denied to everybody else.

{{<lead link="../prepare/networking/">}}
For information on networking and port requirements, refer to [Networking](../prepare/networking/).
{{</lead>}}

## Database authentication

Authentication requires that all clients provide valid credentials before they can connect to a YugabyteDB universe. The authentication credentials in YugabyteDB are stored internally in the YB-Master system tables. The authentication mechanisms available to users depends on what is supported and exposed by the YSQL and YCQL APIs.

You enable authentication for the YSQL and YCQL APIs when you deploy a universe. See [Enable database authentication](authorization-platform/#enable-database-authentication).

YugabyteDB Anywhere and YugabyteDB also support LDAP and OIDC for managing authentication. See [Database authentication](authentication/).

For more information on authentication in YugabyteDB, see [Enable authentication](../../secure/enable-authentication/).

## Role-based access control

Roles can be assigned to grant users only the essential privileges based on the operations they need to perform in YugabyteDB Anywhere, and in YugabyteDB universes.

To manage access to your YugabyteDB Anywhere instance, typically you create a [Super Admin role first](../install-yugabyte-platform/create-admin-user/). The Super Admin can create additional admins and other users with fewer privileges. For information on how to manage YugabyteDB Anywhere users and roles, see [Manage YugabyteDB Anywhere users](../administer-yugabyte-platform/anywhere-rbac/).

For information on how to manage database roles and users, see [Database authorization](authorization-platform/).

## Encryption in transit

Encryption in transit (TLS) ensures that network communication between servers is secure. You can configure YugabyteDB to use TLS to encrypt intra-cluster (Node-to-Node) and client to server (Client-to-Node) network communication. You should enable encryption in transit in YugabyteDB universes and clients to ensure the privacy and integrity of data transferred over the network.

{{<lead link="enable-encryption-in-transit/">}}
For more information, see [Encryption in transit](enable-encryption-in-transit/).
{{</lead>}}

## Encryption at rest

Encryption at rest ensures that data at rest, stored on disk, is protected. You can configure YugabyteDB universes with a user-generated symmetric key to perform universe-wide encryption.

Encryption at rest in YugabyteDB Anywhere uses a master key to encrypt and decrypt universe keys. The master key details are stored in YugabyteDB Anywhere in [key management service (KMS) configurations](create-kms-config/aws-kms/). You enable encryption at rest for a universe by assigning the universe a KMS configuration. The master key designated in the configuration is then used for generating the universe keys used for encrypting the universe data.

{{<lead link="enable-encryption-at-rest/">}}
For more information, see [Enable encryption at rest](enable-encryption-at-rest/).
{{</lead>}}
