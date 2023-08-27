---
title: Security checklist
headerTitle: Security checklist
linkTitle: Security checklist
description: Security measures that can be implemented to protect your YugabyteDB Anywhere and YugabyteDB universes.
menu:
  v2.16_yugabyte-platform:
    parent: security
    identifier: security-checklist-yp
    weight: 10
type: docs
---

You can apply security measures to protect your YugabyteDB Anywhere and YugabyteDB universes.

## Encryption in transit

Encryption in transit (TLS) ensures that network communication between servers is secure. You can configure YugabyteDB to use TLS to encrypt intra-cluster and client to server network communication. It is recommended to enable encryption in transit in YugabyteDB clusters and clients to ensure the privacy and integrity of data transferred over the network.

For more information, see [Enable encryption in transit](../enable-encryption-in-transit).

## Encryption at rest

Encryption at rest ensures that data at rest, stored on disk, is protected. You can configure YugabyteDB with a user-generated symmetric key to perform cluster-wide encryption.
For more information, see [Enable encryption at rest](../enable-encryption-at-rest).

## Role-based access control

Roles can be assigned to grant users only the essential privileges based on the operations they need to perform against YugabyteDB Anywhere. Typically, a Super Admin role is created first. The Super Admin can create additional admins and other users with fewer privileges.

For information on how to enable role-based access control in YugabyteDB Anywhere, see [Authorization](../authorization-platform).

## Authentication

Authentication requires that all clients provide valid credentials before they can connect to a YugabyteDB cluster. The authentication credentials in YugabyteDB are stored internally in the YB-Master system tables. The authentication mechanisms available to users depends on what is supported and exposed by the YSQL, YCQL, and YEDIS APIs.

For more information, see [Enable authentication](../../../secure/enable-authentication/).
