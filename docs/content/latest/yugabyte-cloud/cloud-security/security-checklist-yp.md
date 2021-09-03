<!--
title: Security checklist
headerTitle: Security checklist
linkTitle: Security checklist
description: Security measures that can be implemented to protect your Yugabyte Cloud and YugabyteDB clusters.
menu:
  latest:
    parent: cloud-security
    identifier: security-checklist-yc
    weight: 50
isTocNested: true
showAsideToc: true
-->

Below is a list of security measures that can be implemented to protect your Yugabyte Cloud and YugabyteDB clusters.

## Enable encryption in transit

TLS encryption ensures that network communication between servers is secure. You can configure YugabyteDB to use TLS to encrypt intra-cluster and client to server network communication. Yugabyte recommends enabling encryption in transit in YugabyteDB clusters and clients to ensure the privacy and integrity of data transferred over the network.

Read more about enabling [Encryption in transit](../enable-tls) in Yugabyte Cloud.

## Enable encryption at rest

[Encryption at rest](https://en.wikipedia.org/wiki/Data_at_rest#Encryption) ensures that data at rest, stored on disk, is protected. You can configure YugabyteDB with a user-generated symmetric key to perform cluster-wide encryption. 

Read more about enabling [Encryption at rest](../enable-disk-encryption) in Yugabyte Cloud.

## Configure role-based access control

Roles can be assigned to grant users only the essential privileges based on the operations they need to perform against the platform. Typically, a super admin role is created first. The Super Admin can create additional admins and other fewer privileged users.

To enable role-based access control in Yugabyte Cloud, refer to [Authorization platform](../../cloud-admin/manage-access).

## Limit network exposure

### Restrict machine and port access

Ensure that YugabyteDB runs in a trusted network environment, such that:

* Servers running YugabyteDB services are directly accessible only by the servers running the application and database administrators.

* Only servers running applications can connect to YugabyteDB services on the RPC ports. Access to the [YugabyteDB ports](../../../reference/configuration/default-ports/) should be denied to everybody else.

### Use IP allow lists

Do not assign a public IP address to the nodes running YugabyteDB, if possible. Applications can connect to YugabyteDB over private IP addresses, whitelisted using IP allow lists. Refer to [IP Allow Lists](../../cloud-network/ip-whitelists).

### Use VPC peering

In Amazon Web Services (AWS), run the YugabyteDB cluster in a separate VPC ([Amazon Virtual Private Network](https://docs.aws.amazon.com/vpc/latest/userguide/what-is-amazon-vpc.html)) and peer this only with VPCs from which database access is required; for example, from those VPCs where the application will run. Refer to [VPC Peering](../../cloud-network/vpc-peers).

### Restrict endpoints

Limit the interfaces on which YugabyteDB instances listen for incoming connections. Specify just the required interfaces using endpoints. Refer to [Endpoints](../../cloud-network/endpoints).
