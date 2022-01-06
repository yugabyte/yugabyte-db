---
title: Security architecture
linkTitle: Security architecture
description: Security architecture of Yugabyte Cloud.
image: /images/section_icons/index/secure.png
menu:
  latest:
    parent: cloud-security
    identifier: cloud-security-features
weight: 10
isTocNested: true
showAsideToc: true
---

Yugabyte Cloud is a fully managed YugabyteDB-as-a-Service that allows you to run YugabyteDB clusters on public cloud providers such as Google Cloud Platform (GCP) and Amazon Web Services (AWS), with more public cloud provider options coming soon. Yugabyte Cloud consists of management and data planes. The management plane is a centralized management service deployed on GCP; the management plane is responsible for creating and managing customer data planes. The data plane hosts the customer YugabyteDB clusters deployed on public cloud provider infrastructure.

![Yugabyte Cloud high-level architecture](/images/yb-cloud/cloud-security-diagram.png)

For Yugabyte customers with a paid subscription, Yugabyte creates individual dedicated VPCs for each database cluster within your chosen public cloud provider. For Yugabyte customers that take advantage of a free subscription, clusters are deployed in a shared VPC. All VPCs are firewalled from each other and any other outside connection.

## Infrastructure security

Yugabyte Cloud uses both encryption in transit and encryption at rest to protect clusters and cloud infrastructure.

All communication between Yugabyte Cloud architecture domains is encrypted in transit using TLS. Likewise, all communication between clients or applications and clusters is encrypted in transit. Every cluster has its own certificates, generated when the cluster is created and signed by the Yugabyte internal PKI. Root and intermediate certificates are not extractable from the hardware security appliances.

Data at rest, including clusters and backups, is AES-256 encrypted using native cloud provider technologies - S3 and EBS volume encryption for AWS, and server-side and persistent disk encryption for GCP. Encryption keys are managed by the cloud provider and anchored by hardware security appliances. Customers can enable [column-level encryption](../../../secure/column-level-encryption) as an additional security control.

Yugabyte Cloud provides DDoS and application layer protection, and automatically blocks network protocol and volumetric DDoS attacks.

## Securing database clusters by default

Yugabyte secures Yugabyte Cloud databases using the same default [security measures that we recommend to our customers](../../../secure/security-checklist/) to secure their own YugabyteDB installations, including:

- authentication
- role-based access control
- dedicated users
- limited network exposure
- encryption in transit
- encryption at rest

## Data privacy and compliance

For information on data privacy and compliance, refer to the [Yugabyte DPA](https://www.yugabyte.com/yugabyte-cloud-data-processing-addendum/).

## Auditing

Yugabyte supports audit logging at the cloud account and database level.

For information on database audit logging, refer to [Configure Audit Logging](../../../secure/audit-logging/audit-logging-ysql/).
