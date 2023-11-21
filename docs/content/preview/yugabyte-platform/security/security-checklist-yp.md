---
title: Security checklist for YugabyteDB Anywhere
headerTitle: Security checklist
linkTitle: Security checklist
description: Security measures that can be implemented to protect your YugabyteDB Anywhere and YugabyteDB universes.
menu:
  preview_yugabyte-platform:
    parent: security
    identifier: security-checklist-yp
    weight: 10
type: docs
---

You can apply security measures to protect your YugabyteDB Anywhere instance and YugabyteDB universes.

## Authentication

Authentication requires that all clients provide valid credentials before they can connect to a YugabyteDB universe. The authentication credentials in YugabyteDB are stored internally in the YB-Master system tables. The authentication mechanisms available to users depends on what is supported and exposed by the YSQL, YCQL, and YEDIS APIs.

You enable authentication for the YSQL and YCQL APIs when you deploy a universe. See [Create YugabyteDB universe deployments](../../create-deployments/).

YugabyteDB Anywhere and YugabyteDB also support LDAP and OIDC for managing authentication. See [Authentication](../authentication/)

For more information on authentication in YugabyteDB, see [Enable authentication](../../../secure/enable-authentication/).

## Role-based access control

Roles can be assigned to grant users only the essential privileges based on the operations they need to perform in YugabyteDB Anywhere, and in YugabyteDB universes.

To manage access to your YugabyteDB Anywhere instance, typically you create a [Super Admin role first](../../configure-yugabyte-platform/create-admin-user/). The Super Admin can create additional admins and other users with fewer privileges. For information on how to manage YugabyteDB Anywhere users and roles, see [Manage YugabyteDB Anywhere users](../../administer-yugabyte-platform/rbac-platform/).

For information on how to manage database roles and users, see [Database authorization](../authorization-platform).

## Encryption in transit

Encryption in transit (TLS) ensures that network communication between servers is secure. You can configure YugabyteDB to use TLS to encrypt intra-cluster and client to server network communication. It is recommended to enable encryption in transit in YugabyteDB universes and clients to ensure the privacy and integrity of data transferred over the network.

For more information, see [Enable encryption in transit](../enable-encryption-in-transit).

## Encryption at rest

Encryption at rest ensures that data at rest, stored on disk, is protected. You can configure YugabyteDB universes with a user-generated symmetric key to perform universe-wide encryption.

For more information, see [Enable encryption at rest](../enable-encryption-at-rest).
