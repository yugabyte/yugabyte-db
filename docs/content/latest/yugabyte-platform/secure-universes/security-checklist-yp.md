---
title: Security checklist
headerTitle: Security checklist
linkTitle: Security checklist
description: Security measures that can be implemented to protect your Yugabyte Platform and YugabyteDB universes.
menu:
  latest:
    parent: secure-universes
    identifier: security-checklist
    weight: 20
isTocNested: true
showAsideToc: true
---

Below is a list of security measures that can be implemented to protect your Yugabyte Platform and YugabyteDB universes.

## Enable encryption in transit

TLS encryption ensures that network communication between servers is secure. You can configure YugabyteDB to use TLS to encrypt intra-cluster and client to server network communication. Yugabyte recommends enabling encryption in transit in YugabyteDB clusters and clients to ensure the privacy and integrity of data transferred over the network.

Read more about enabling [Encryption in transit](../enable-encryption-at-rest) in YugabyteDB.
 
## Enable encryption at rest

[Encryption at rest](https://en.wikipedia.org/wiki/Data_at_rest#Encryption) ensures that data at rest, stored on disk, is protected. You can configure YugabyteDB with a user-generated symmetric key to perform cluster-wide encryption.
Read more about enabling Encryption at rest in YugabyteDB.

## Configure role-based access control

Roles can be assigned to grant users only the essential privileges based on the operations they need to perform against the platform. Typically, a super admin role is created first. The super admin can create additional admins and other fewer privileged users.

See the authorization section to enable role-based access control in Yugabyte Platform.

## Enable authentication

Authentication requires that all clients provide valid credentials before they can connect to a YugabyteDB cluster. The authentication credentials in YugabyteDB are stored internally in the YB-Master system tables. The authentication mechanisms available to users depends on what is supported and exposed by the YSQL, YCQL, and YEDIS APIs.

Read more about how to enable authentication in YugabyteDB.

## Network Security

To ensure that Yugabyte Platform YugabyteDB runs in a trusted network environment you can restrict machine and port access. Here are some steps to ensure that.

* Servers running YugabyteDB services are directly accessible only by the Yugabyte Platform, servers running the application, and database administrators.
* Only Yugabyte Platform and servers running applications can connect to YugabyteDB services on the RPC ports. Access to the YugabyteDB ports should be denied to everybody else.

Check the list of default ports that need to be opened on the YugabyteDB servers for the Yugabyte Platform and other applications to connect.
