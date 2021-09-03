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

Yugabyte Cloud is a fully-managed YugabyteDB-as-a-Service that allows you to run YugabyteDB clusters on cloud providers (Google Cloud Platform (GCP), Amazon Web Services (AWS)). It consists of management and data planes. The management plane is a centralized management service deployed on GCP that is responsible for creating and managing customer data planes. The data plane is the customer YugabyteDB clusters deployed on cloud provider infrastructure.

As such, Yugabyte Cloud security is a shared responsibility between the cloud providers, Yugabyte, and you, the customer. Under this shared responsibility model, Yugabyte implements the technical and organizational security measures as described in the [Yugabyte Cloud - Data Processing Addendum (CCPA & GDPR)](). 

Yugabyte is responsible for making the management plane more secure. The management plane includes the GCP plane, cloud provider planes, VMs, API servers, operating systems, and various other controllers. All of your data plane components run on GCP and AWS infrastructure operated by Yugabyte. Your data is processed at the Yugabyte Cloud account level, and each account is a single tenant, meaning it runs its components for only one customer. 

You are responsible for the security, protection, and backup of your data, and your use and configuration of Yugabyte Cloud. It is also your responsibility to evaluate Yugabyte Cloud security to determine whether your data can be stored in Yugabyte Cloud.

The cloud providers and Yugabyte manage the following components:

- The GCP management plane, including the VMs, API server, other components on the VMs, and etcd
- The AWS and GCP data planes on which the clusters are deployed, including the VMs, API server, other components on the VMs, and etcd
- The Kubernetes distribution
- The cluster nodes' operating system
- YugabyteDB cluster infrastructure

Configuration related to these components is generally not available for you to audit or modify in Yugabyte Cloud. You are responsible for configuring Yugabyte Cloud and your clusters, and can audit and remediate any recommendations for those components. 

### Securing cluster configurations

Protecting workloads is your responsibility. Yugabyte Cloud runs on Yugabyte Platform and offers the same security hardening options. Refer to [Yugabyte Platform Security](../../yugabyte-platform/security/).

### Infrastructure security

Yugabyte Cloud uses both encryption in transit and encryption at rest to protect clusters and cloud infrastructure.

All communication between Yugabyte Cloud architecture domains is encrypted in transit using TLS. Likewise, all communication between clients or applications and clusters is encrypted in transit. Every cluster has its own certificates, generated when the cluster is created and signed by internal PKI managed by Yugabyte. Root and intermediate certificates are not extractable from the hardware security appliances.

Data at rest, including clusters and backups, is AES-256 encrypted. The encryption keys are managed by Yugabyte and anchored by hardware security appliances. 

Yugabyte Cloud provides DDoS and application layer protection, and automatically blocks network protocol and volumetric DDoS attacks.

### Data privacy and compliance

For information on data privacy and compliance, refer to [Data privacy and compliance]().

## Auditing

Yugabyte supports audit logging at the cloud account and database level.

Cloud-related audit logs can be provided on request. Contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431).

For information on database audit logging, refer to [Configure Audit Logging](../../secure/audit-logging/audit-logging-ysql/).
