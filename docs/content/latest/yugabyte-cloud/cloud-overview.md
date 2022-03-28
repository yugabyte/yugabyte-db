---
title: Yugabyte Cloud
linkTitle: Overview
description: Fully managed YugabyteDB-as-a-Service.
headcontent: Fully managed YugabyteDB-as-a-Service
image: /images/section_icons/index/quick_start.png
section: YUGABYTE CLOUD
aliases:
  - /latest/yugabyte-cloud/
  - /latest/deploy/yugabyte-cloud/
menu:
  latest:
    identifier: cloud-overview
    weight: 14
isTocNested: true
showAsideToc: true
---

Yugabyte Cloud is a fully managed YugabyteDB-as-a-Service that allows you to run YugabyteDB clusters on <a href="https://cloud.google.com/">Google Cloud Platform (GCP)</a> and <a href="https://aws.amazon.com/">Amazon Web Services (AWS)</a> (as of September 2021; Azure coming soon, for information contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431)).

### Start here

To begin using Yugabyte Cloud, go to [Quick start](../cloud-quickstart/).

### In depth

|  | |
| :--- | :--- |
| **Cluster basics** |  |
| [Deploy](../cloud-basics/) | Create single region clusters that can be deployed across multiple and single availability zones. (To deploy multi-region clusters, contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431).)<br>Create virtual private cloud (VPC) networks to peer clusters with application VPCs.
| [Secure](../cloud-secure-clusters/) | Manage cluster security features, including network authorization, database authorization, encryption, and auditing.<br> Use IP allow lists to manage access to clusters. |
| [Connect](../cloud-connect/) | Connect to clusters from a browser using Cloud Shell, from your desktop using a shell, and from applications. |
| [Monitor](../cloud-monitor/) | Monitor cluster performance and get notified of potential problems. |
| [Manage](../cloud-clusters/) | Scale vertically and horizontally to match your performance requirements.<br>Back up and restore clusters.<br>Create PostgreSQL extensions.<br>Set maintenance windows for cluster maintenance and database upgrades.<br>Pause, resume, and delete clusters. |
| **Cloud basics** | |
| [Add cloud users](../cloud-admin/manage-access/) | Invite team members to your cloud. |
| [Manage billing](../cloud-admin/cloud-billing-profile/) | Create your billing profile, manage your payment methods, and review invoices. |
| **Reference** | |
| [What's new](../release-notes/) | See what's new in Yugabyte Cloud, what regions are supported, and known issues. |
| [Troubleshooting](../cloud-troubleshoot/) | Get solutions to common problems. |
| [FAQ](../cloud-faq/) | Get answers to frequently asked questions. |
| [Security architecture](../cloud-security/) | Review Yugabyte Cloud's security architecture and shared responsibility model. |
| [Cluster costs](../cloud-admin/cloud-billing-costs/) | Review how Yugabyte Cloud charges for clusters. |
