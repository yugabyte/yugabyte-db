---
title: Shared responsibility model
linkTitle: Shared responsibility model
description: Yugabyte Cloud security shared responsibility model.
image: /images/section_icons/index/secure.png
menu:
  latest:
    parent: cloud-security
    identifier: shared-responsibility
weight: 20
isTocNested: true
showAsideToc: true
---

Yugabyte Cloud security and compliance is a shared responsibility between public cloud providers, Yugabyte, and Yugabyte Cloud customers.

Under the shared responsibility model, there is a division of responsibility for the security aspects of Yugabyte Cloud, which includes but is not limited to network controls, data classification, application controls, identity and access management, and more. The responsibilities for each workload and feature depend on where the workload is hosted - Software as a Service (SaaS), Platform as a Service (PaaS), Infrastructure as a Service (IaaS), or on-premises in your own data center.

![Shared responsibility model](/images/yb-cloud/cloud-shared-responsibility.png)

You are responsible for reviewing Yugabyte Cloud security capabilities and features in accordance with your compliance and regulatory requirements to determine whether your data can be stored in Yugabyte Cloud. You are also responsible for the security, protection, and backup of your data as far as this is possible in the shared responsibility model, as well as for configuring Yugabyte Cloud security and product features to the extent that Yugabyte makes it possible.

Yugabyte leverages public cloud provider PaaS and SaaS offerings to deliver the Yugabyte Cloud service. Under the shared responsibility model, the public cloud providers are responsible for providing and protecting the infrastructure (the hardware, software, networking, and facilities) that runs the cloud services Yugabyte provides in Yugabyte Cloud. Configuration related to infrastructure managed and operated by the public cloud providers is generally not available for Yugabyte customers to audit or modify in Yugabyte Cloud.

Yugabyte implements the technical and organizational security measures as described in the [Yugabyte Cloud Data Processing Addendum (DPA)](https://www.yugabyte.com/yugabyte-cloud-data-processing-addendum/).
