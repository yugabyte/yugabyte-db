---
title: Cloud security
headerTitle: Yugabyte Cloud security
linkTitle: Security
description: Yugabyte Cloud security shared responsibility model.
image: /images/section_icons/index/secure.png
headcontent: The Yugabyte Cloud shared responsibility model for security.
menu:
  latest:
    parent: yugabyte-cloud
    identifier: cloud-security
weight: 800
---

Yugabyte Cloud is a fully managed YugabyteDB-as-a-Service that allows you to run YugabyteDB clusters on public cloud providers such as Google Cloud Platform (GCP) and Amazon Web Services (AWS), with more public cloud provider options coming soon. Yugabyte Cloud consists of management and data planes. The management plane is a centralized management service deployed on GCP; the management plane is responsible for creating and managing customer data planes. The data plane hosts the customer YugabyteDB clusters deployed on public cloud provider infrastructure. 

![Yugabyte Cloud high-level architecture](/images/yb-cloud/cloud-security-diagram.png)

For Yugabyte customers with a paid subscription, Yugabyte creates individual dedicated VPCs for each database cluster within your chosen public cloud provider. For Yugabyte customers that take advantage of a free subscription, clusters are deployed in a shared VPC. All VPCs are firewalled from each other and any other outside connection.

## The shared responsibility model

Yugabyte Cloud security and compliance is a shared responsibility between public cloud providers, Yugabyte, and Yugabyte Cloud customers.

Under the shared responsibility model, there is a division of responsibility for the security aspects of Yugabyte Cloud, which includes but is not limited to network controls, data classification, application controls, identity and access management, and more. The responsibilities for each workload and feature depend on where the workload is hosted - Software as a Service (SaaS), Platform as a Service (PaaS), Infrastructure as a Service (IaaS), or on-premises in your own data center.

![Shared responsibility model](/images/yb-cloud/cloud-shared-responsibility.png)

You are responsible for reviewing Yugabyte Cloud security capabilities and features to determine whether your data can be stored in Yugabyte Cloud. You are also responsible for the security, protection, and backup of your data, as well as your use and configuration of Yugabyte Cloud. 

Public cloud providers are responsible for protecting the infrastructure that runs all of the cloud services. This infrastructure is composed of the hardware, software, networking, and facilities. Configuration related to these components is generally not available for you to audit or modify in Yugabyte Cloud. You are responsible for configuring Yugabyte Cloud and your clusters, and can audit and remediate any recommendations for those components.

Yugabyte implements the technical and organizational security measures as described in the [Yugabyte Cloud Data Processing Addendum (DPA)](https://www.yugabyte.com/yugabyte-cloud-data-processing-addendum/). 

## Infrastructure security

Yugabyte Cloud uses both encryption in transit and encryption at rest to protect clusters and cloud infrastructure.

All communication between Yugabyte Cloud architecture domains is encrypted in transit using TLS. Likewise, all communication between clients or applications and clusters is encrypted in transit. Every cluster has its own certificates, generated when the cluster is created and signed by the Yugabyte internal PKI. Root and intermediate certificates are not extractable from the hardware security appliances.

Data at rest, including clusters and backups, is AES-256 encrypted. Encryption keys are managed by Yugabyte and anchored by hardware security appliances. Customers can enable [column-level encryption](../../secure/column-level-encryption) as an additional security control. 

Yugabyte Cloud provides DDoS and application layer protection, and automatically blocks network protocol and volumetric DDoS attacks.

## Securing database clusters by default

Yugabyte secures Yugabyte Cloud using the same default [security measures that we recommend to our customers](../../secure/security-checklist/) to secure their own YugabyteDB installations, including:

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

Cloud-related audit logs can be provided on request. Contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431).

For information on database audit logging, refer to [Configure Audit Logging](../../secure/audit-logging/audit-logging-ysql/).
