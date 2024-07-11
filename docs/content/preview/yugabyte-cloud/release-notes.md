---
title: Change log for YugabyteDB Aeon
headerTitle: What's new in YugabyteDB Aeon
linkTitle: Change log
description: YugabyteDB Aeon Change Log and known issues.
headcontent: New features and known issues
menu:
  preview_yugabyte-cloud:
    identifier: yugabytedb-managed-releases
    parent: yugabytedb-managed
    weight: 22
type: docs
---

On this page:

[Change log](#change-log) \
[Known issues](#known-issues)

## Change log

### July 11, 2024

**Database**

- Ability to choose the latest [stable release](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) when creating a dedicated or Sandbox cluster using the new Early Access track. Early Access has more frequent updates for faster access to the most recent stable version of YugabyteDB. Currently features YugabyteDB v2024.1.0.0.
- Production track updated to 2.18.8.0.
- Innovation track updated to 2.20.4.1.
- Preview track updated to 2.21.1.0.

### June 24, 2024

**New Features**

- YugabyteDB Managed is now YugabyteDB Aeon! [Learn more](https://www.yugabyte.com/blog/introducing-yugabytedb-aeon/).
- Support for new pricing plans. Existing customers continue to be billed using classic pricing. [Learn more](https://www.yugabyte.com/pricing/).
- Support for enforcing security features on new clusters using the Advanced security profile option (Enterprise plan only).

### June 13, 2024

**New Features**

- Support for exporting [PostgreSQL logs](https://www.postgresql.org/docs/11/runtime-config-logging.html) to third-party tools (such as Datadog) for security monitoring, to build operations and health dashboards, troubleshooting, and more.
- New regions. Jakarta and Hyderabad on AWS, and Singapore on Azure.

**Database**

- Production track updated to 2.14.17.0.

### April 22, 2024

**New Features**

- Support for incremental backups for faster backups with greater frequency. Incremental backups only include the data that has changed since the last backup, be it a full or incremental backup.
- Ability to size each region in partition by region clusters to its load. Number of nodes, number of vCPUs, disk size, and IOPS can now be set independently for each region. Add extra horsepower in high-traffic regions, and provision lower-traffic regions with fewer nodes.

### April 17, 2024

**Database**

- Ability to choose from different tracks for Sandbox clusters. Sandbox clusters now default to using the Innovation track; you can also choose the Preview track.
- Innovation track updated to 2.18.7.0.

### February 28, 2024

**Database**

- Production track updated to 2.14.15.0.

### February 8, 2024

**New Features**

- Support for PingOne [federated authentication](../managed-security/managed-authentication/), which allows single sign-on access for your account users using their PingOne identity.

    Blog: [Enhanced Identity Security with Okta and PingOne Single Sign-On Integrations](https://www.yugabyte.com/blog/single-sign-on-okta-pingone/)

### January 31, 2024

**New Features**

- Support for Okta [federated authentication](../managed-security/managed-authentication/), which allows single sign-on access for your account users using their Okta identities.

    Blog: [Enhanced Identity Security with Okta and PingOne Single Sign-On Integrations](https://www.yugabyte.com/blog/single-sign-on-okta-pingone/)

**Database**

- Innovation track updated to 2.18.5.0.

### December 27, 2023

**New Features**

- Support for enhanced [fault tolerance](../cloud-basics/create-clusters-overview/#fault-tolerance). Clusters are fault tolerant, meaning they continue to serve reads and writes even with the loss of a node, availability zone, or region. You can now configure clusters with node- or region-level fault tolerance to be resilient to up to three domain outages. For example, you can create a cluster with region-level fault tolerance that can continue to serve reads and writes without interruption even if two of its regions become unavailable.

### December 4, 2023

**New Features**

- Support for [federated authentication](../managed-security/managed-authentication/), which allows you to use an identity provider to manage access to your account. Initial support includes the Microsoft Entra ID (Azure AD) platform, providing single sign-on access for your account users using their Microsoft identities.
- Added ability to [audit account login activity](../cloud-secure-clusters/cloud-activity/). Navigate to **Security > Activity > Access History** to review the access history, including the client IP address, activity type, number of attempts, timestamp, and result.
- Added ability to use different instance types and node sizes for different [read replica regions](../cloud-clusters/managed-read-replica/) in a cluster. Specify higher vCPU and disk size per node for replicas in high traffic regions, and vice-versa for lower traffic regions.
- Support for Azure Key Vault for enabling and disabling YugabyteDB [encryption at rest](../cloud-secure-clusters/managed-ear/) using a customer managed key.

**Database**

- Production track updated to 2.14.14.0.

### November 16, 2023

**New Features**

- [Product Labs](../../yugabyte-cloud/managed-labs/) provides an interactive, in-product learning experience. Learn about YugabyteDB features using real-world applications running on live YugabyteDB clusters. The first lab, Create Global Applications, demonstrates how to manage latencies using three different deployment strategies.
- Support for [exporting cluster metrics](../cloud-monitor/managed-integrations/) to Sumo Logic.

**Database**

- Innovation track updated to 2.18.4.1.

### November 3, 2023

**New Features**

- Ability to track usage per cluster over time. Navigate to **Usage & Billing > Usage** to view cumulative and daily usage of cluster compute, disk storage, cloud backup storage, and data transfer.
- For Azure, all regions that are supported for single-region clusters are now available for multi-region clusters (including Virginia useast2, Tokyo, Seoul, Johannesburg, Texas, and Dubai).

**Database**

- Production track updated to 2.14.13.0.
- Preview track updated to 2.19.3.0.

### October 5, 2023

**New Features**

- Support for creating [private service endpoints](../cloud-basics/cloud-vpcs/cloud-add-endpoint/) (PSEs) in the YugabyteDB Aeon UI (this feature was previously only available using the YugabyteDB Aeon CLI). Add PSEs to clusters to connect to your application VPC over a secure private link. Supports AWS PrivateLink and Azure Private Link.
- Support for [exporting cluster metrics](../cloud-monitor/managed-integrations/) to Grafana Cloud.

### September 22, 2023

**Database**

- Preview track updated to 2.19.2.0.
- Production track updated to 2.14.12.0.

### September 12, 2023

**New Features**

- Support for multi-region clusters and read replicas in Azure. You can now create dedicated clusters with multi-region deployment in Azure, as well as read replicas.

### September 6, 2023

**New Features**

- Support for enabling and disabling YugabyteDB [encryption at rest](../cloud-secure-clusters/managed-ear/) using a customer managed key and rotating keys on encrypted clusters. Clusters must be using YugabyteDB v2.16.7 or later.
- Support for [exporting cluster metrics](../cloud-monitor/managed-integrations/) to Datadog.

**Database**

- Innovation track updated to 2.18.2.1.

### August 15, 2023

**Database**

- Innovation track updated to 2.16.6.0.
- Production track updated to 2.14.11.0.

### July 13, 2023

**Database**

- Ability to choose from different tracks in the [stable release series](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) when creating a dedicated cluster. You can choose one of the following tracks:
  - Innovation track - has more frequent updates for faster access to new features. Currently features YugabyteDB version 2.16.5.0.
  - Production track - has a slower update cadence and features only select stable release builds. Currently features YugabyteDB version 2.14.10.2.

  After a cluster is created, it is upgraded with releases from the release track selected at creation. Clusters previously on the Stable track running 2.14 are now on the Production track, while clusters running 2.16 are now on the Innovation track. Sandbox clusters continue to use the Preview release track.

### July 3, 2023

**Database**

- [Preview release](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) updated to [version 2.19.0](../../releases/ybdb-releases/v2.19/#v2.19.0.0). New Sandbox clusters automatically use the new version. Existing clusters will be upgraded in the coming weeks.

### June 30, 2023

**New Features**

- Support for deploying clusters on Microsoft Azure, including:
  - Global availability - deploy in 20 Azure regions worldwide.
  - Azure Private Link - establish secure private network connectivity between your Azure Virtual Networks and YugabyteDB Aeon clusters for greater data privacy and compliance.
  - Horizontal and vertical scaling.
  - Availability zone fault tolerance - deploy single region, multi-zone clusters to ensure high availability and fault tolerance in the same region.

  Blog: [Presenting the Enterprise-Ready DBaaS on Azure](https://www.yugabyte.com/blog/enterprise-ready-dbaas-on-azure/)

<div style="position: relative; padding-bottom: calc(51.625% + 44px); height: 0;"><iframe src="https://app.supademo.com/embed/_AUexrYwJ1vNyKQsdA_di" frameborder="0" webkitallowfullscreen="true" mozallowfullscreen="true" allowfullscreen style="position: absolute; top: 0; left: 0; width: 100%; height: 100%;"></iframe></div>

### June 26, 2023

**New Features**

- Support for using a customer managed key (CMK) in Google Cloud Key Management Service (KMS) to encrypt a dedicated cluster (preview release). When YugabyteDB encryption at rest is enabled, your can now encrypt your cluster using your own CMK residing in Google Cloud KMS or AWS KMS.

### June 8, 2023

**Database**

- [Preview release](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) updated to [version 2.17.3](../../releases/ybdb-releases/v2.17/#v2.17.3.0). New Sandbox clusters automatically use the new version. Existing clusters will be upgraded in the coming weeks.

### June 7, 2023

**New Features**

- Ability to create custom roles using Role-Based Access Control (RBAC) to precisely manage user access and permissions to match your organization's specific needs. This includes a new built-in Viewer role which offers a secure and restricted view of cluster information without the risk of unintended modifications.

### May 30, 2023

**Database**

- [Stable release](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) updated to [version 2.14.9](../../releases/ybdb-releases/v2.14/#v2.14.9.0). New clusters use this version by default.

### April 28, 2023

**New Features**

- Support for using a customer managed key (CMK) to encrypt a dedicated cluster (preview release). When YugabyteDB encryption at rest is enabled, your cluster (including backups) is encrypted using your own CMK residing in AWS Key Management Service (KMS).

### March 31, 2023

**Database**

- [Preview release](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) updated to [version 2.17.2](../../releases/ybdb-releases/v2.17/#v2.17.2.0). New Sandbox clusters automatically use the new version. Existing clusters will be upgraded in the coming weeks.

### March 27, 2023

**New Features**

- [YugabyteDB Aeon CLI](../../yugabyte-cloud/managed-automation/managed-cli/). Use the YugabyteDB Aeon command line interface (ybm CLI) to deploy and manage your YugabyteDB Aeon database clusters from your terminal or IDE.
- Support for AWS PrivateLink (preview release). Connect YugabyteDB Aeon clusters on AWS with other AWS resources via private endpoints. Currently only configurable via ybm CLI.

**Enhancements**

- Faster cluster scaling. Up to 2x performance in vertical scaling (adding and removing vCPUs) operations on dedicated clusters.
- Support for more than 125 additional performance metrics.
- Ability to reorder performance metrics to create customized dashboards.

### March 16, 2023

**Database**

- [Stable release](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) updated to [version 2.14.7](../../releases/ybdb-releases/v2.14/#v2.14.7.0) for dedicated clusters. New clusters use this version by default.

### March 1, 2023

**Database**

- [Stable release](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) reset to [version 2.12.9](../../releases/ybdb-releases/end-of-life/v2.12/#v2.12.9.0) for dedicated clusters. New clusters use this version by default.

### February 13, 2023

**Database**

- [Preview release](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) updated to version [2.17.1](../../releases/ybdb-releases/v2.17/#v2.17.1.0). New Sandbox clusters automatically use the new version. Existing clusters will be upgraded in the coming weeks.

- [Stable release](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) reset to [version 2.14.6](../../releases/ybdb-releases/v2.14/#v2.14.6.0) for dedicated clusters. New clusters use this version by default.

### February 8, 2023

**New Features**

- Users can now request a [time-limited free trial](../managed-freetrial/) to explore all the YugabyteDB Aeon features.

**Database**

- [Stable release](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) updated to version 2.16.1 for dedicated clusters. New clusters use the new version by default. Existing clusters will be upgraded in the coming weeks.

### January 27, 2023

**New Features**

- [YugabyteDB Aeon Terraform Provider](https://registry.terraform.io/providers/yugabyte/ybm/latest) generally available. Use the provider to deploy and manage your database clusters in YugabyteDB Aeon.

### January 18, 2023

**Database**

- [Stable release](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) updated to version 2.16.0 for dedicated clusters. New clusters use the new version by default. Existing clusters will be upgraded in the coming weeks.

**Infrastructure**

- Instance type for new Dedicated clusters on AWS updated to [m6i](https://aws.amazon.com/ec2/instance-types/m6i/).

### January 13, 2023

**Database**

- [Stable release](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) updated to [version 2.14.6](../../releases/ybdb-releases/v2.14/#v2.14.6.0) for dedicated clusters. New clusters use the new version by default. Existing clusters will be upgraded in the coming weeks.

### December 21, 2022

**New Features**

- Ability to add IP addresses to the cluster IP allow list during cluster creation. The **Create Cluster** wizard includes the new **Networking** page to configure connectivity for your cluster. Automatically detect and add your current IP address or the addresses of any peered VPC to the cluster.
- Ability to connect to clusters deployed in VPCs from public IP addresses. For clusters deployed in VPCs, enable **Public Access** on the **Settings > Network Access** tab to connect from addresses outside the peered network. When enabled, a public IP address is added to each region of the cluster. You can view the private and public host addresses under **Connection Parameters** on the cluster **Settings > Infrastructure** tab.

**Database**

- [Stable release](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) updated to [version 2.14.5](../../releases/ybdb-releases/v2.14/#v2.14.5.0) for dedicated clusters. New clusters use the new version by default. Existing clusters will be upgraded in the coming weeks.

### December 12, 2022

**New Features**

- [YugabyteDB Aeon REST API](https://yugabyte.stoplight.io/docs/managed-apis/) generally available. Use the REST API to deploy and manage your database clusters in YugabyteDB Aeon programmatically.

### November 28, 2022

**New Features**

- Support for multi-region clusters with [geo-partitioning](../../explore/multi-region-deployments/row-level-geo-partitioning/) using the new [Partition by region](../cloud-basics/create-clusters-topology/#partition-by-region) deployment. Geo-partitioning allows you to move data closer to users to achieve lower latency and higher performance, and meet data residency requirements to comply with regulations such as GDPR.
- Support for [read replicas](../cloud-basics/create-clusters-topology/#read-replicas). Use read replicas to lower latencies for read requests from remote regions.

**Database**

- [Preview release](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) updated to version 2.15.3. New Sandbox clusters automatically use the new version. Existing clusters will be upgraded in the coming weeks.
- Stable release updated to version 2.14.4 for dedicated clusters. New clusters use the new version by default. Existing clusters will be upgraded in the coming weeks.

### November 15, 2022

**New Features**

- Ability to view cluster health. YugabyteDB Aeon monitors the health of your clusters based on cluster alert conditions and displays the health as either Healthy, Needs Attention, or Unhealthy.
- Ability to set alerts for failed nodes. Get notified when the number of failed nodes exceeds the threshold.

### November 4, 2022

**New Features**

- Ability to reset slow queries for faster debugging of slow-running queries.
- Ability to set a preferred region to tune the read and write latency for specific regions. Designating one region as preferred can reduce the number of network hops needed to process requests. The preferred region can be assigned during cluster creation, and set or changed after cluster creation.
- Ability to view details of task progress for cluster edit operations for better monitoring.

### October 24, 2022

**New Features**

- Support for role-based API keys. Assign [roles](../managed-security/managed-roles) to API keys; keys assigned a developer role can't be used to perform admin tasks. In addition, keys are no longer revoked if the user that created the key is deleted from the account.

### October 17, 2022

**New Features**

- Ability to set alerts for cluster memory use and YSQL connections. Get notified when memory use or the number of YSQL connections in a cluster exceeds the threshold. High memory use or number of YSQL connections can indicate problems with your workload, such as unoptimized queries or problems with your application connection code.

### September 28, 2022

**Database**

- [Preview release](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) updated to version 2.15.2. New Sandbox clusters automatically use the new version. Existing clusters will be upgraded in the coming weeks.

### September 19, 2022

**Database**

- Stable release updated to version 2.14.2 for dedicated clusters. New clusters use the new version by default. Existing clusters will be upgraded in the coming weeks.

### August 17, 2022

**Database**

- Stable release updated to version 2.14.1 for dedicated clusters. New clusters use the new version by default. Existing clusters will be upgraded in the coming weeks.

### July 8, 2022

**Database**

- Preview release updated to version 2.15.0. New Sandbox clusters automatically use the new version. Existing clusters will be upgraded in the coming weeks.

### June 27, 2022

**New Features**

- Performance Optimizer for scanning clusters for optimizations (preview release). Provides recommendations on index and schema improvements, and detects connection, query, and CPU skew to identify potentially hot nodes.
- [YugabyteDB Aeon REST API](https://yugabyte.stoplight.io/docs/managed-apis/) (preview release). Use the REST API to deploy and manage your database clusters in YugabyteDB Aeon programmatically.
- API key management for creating and managing bearer tokens for use with the YugabyteDB Aeon REST API.

### June 22, 2022

**New Features**

- Support for creating multi-region replicated clusters (preview release). Create clusters that are resilient to region-level outages, with data synchronously replicated across 3 regions.

**Database**

- Stable release updated to version 2.12.6 for dedicated clusters. New clusters use the new version by default. Existing clusters will be upgraded in the coming weeks.

**Infrastructure**

- Instance type for new Dedicated clusters on AWS updated to [m5](https://aws.amazon.com/ec2/instance-types/m5/).

### June 14, 2022

**New Features**

- Support for social logins. Sign up and log in to YugabyteDB Aeon using your existing Google, LinkedIn, or GitHub account. Admin users can manage the available login methods from the **Authentication** tab on the **Security** page.

**Database**

- Stable release updated to version 2.12.5 for dedicated clusters. New clusters use the new version by default. Existing clusters will be upgraded in the coming weeks.

### May 18, 2022

**Enhancements**

- Faster cluster creation. Create most clusters in under five minutes.

**Database**

- [Preview release](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) updated to version 2.13.1. New Sandbox clusters automatically use the new version. Existing clusters will be upgraded in the coming weeks.
- Stable release updated to version 2.12.3 for Dedicated clusters. New Dedicated clusters use the new version by default. Existing clusters will be upgraded in the coming weeks.

**Fixes**

- Cloud Shell is now available for clusters in a VPC.

### March 31, 2022

**New Features**

- Self-guided quickstart incorporated in Cloud Shell. Launch Cloud Shell using the YSQL API to begin a [self-guided tutorial](../cloud-quickstart/qs-explore/) exploring distributed SQL.

**Enhancements**

- Support in Cloud Shell to allow longer sessions (up to one hour) and up to five concurrent sessions.

### March 10, 2022

**New Features**

- Ability to schedule the maintenance window and exclusion periods for upcoming maintenance and database upgrades. The maintenance window is a weekly four-hour time slot during which Yugabyte may maintain or upgrade clusters. Yugabyte does not maintain or upgrade clusters outside the scheduled maintenance window, or during exclusion periods. Manage maintenance windows on the cluster **Maintenance** tab.
- Ability to manually pause and resume clusters. To pause a cluster, select the cluster, click **Actions**, and choose **Pause Cluster**. Yugabyte suspends instance vCPU capacity charges for paused clusters; disk and backup storage are charged at the standard rate.

### February 3, 2022

**New Features**

- Ability to select the [version](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) of YugabyteDB to install on a cluster when [creating Dedicated clusters](../cloud-basics/create-clusters/).
- Automated notifications of upcoming database maintenance. The notification email includes the date and time of the maintenance window. An Upcoming Maintenance badge is also displayed on the cluster. Start an upgrade any time by signing in to YugabyteDB Aeon, selecting the cluster, clicking the **Upcoming Maintenance** badge, and clicking **Upgrade Now**.

**Infrastructure**

- Instance type for new Sandbox clusters on AWS updated to [EC2 T3.small](https://aws.amazon.com/ec2/instance-types/t3/). Existing Sandbox clusters retain their EC2 T2.small instance type for their lifetime.
- Instance type for new Dedicated clusters on GCP updated to [n2-standard](https://cloud.google.com/compute/docs/general-purpose-machines#n2_machines).

**Database**

- [Preview release](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) updated to version 2.11.1. New Sandbox clusters automatically use the new version. Existing clusters will be upgraded in the coming weeks.
- Stable release updated to version 2.8.1 for Dedicated clusters. New Dedicated clusters use the new version by default. Existing clusters will be upgraded in the coming weeks.

### January 27, 2022

**New Features**

- Support for [alerts](../cloud-monitor/cloud-alerts/) to notify you and your team members when cluster and database resource usage exceeds predefined limits, or of potential billing issues. Configure alerts and view notifications on the **Alerts** page. When an alert triggers, YugabyteDB Aeon sends an email notification and displays a notification on the **Notifications** tab. When the alert condition resolves, the notification dismisses automatically. Alerts are enabled for all clusters in your account.
- Sandbox clusters are now [paused](../../faq/yugabytedb-managed-faq/#why-is-my-sandbox-cluster-paused) after 21 days of inactivity. YugabyteDB Aeon sends a notification when your cluster is paused. To keep a cluster from being paused, perform an action as described in [What qualifies as activity on a cluster?](../../faq/yugabytedb-managed-faq/#what-qualifies-as-activity-on-a-cluster) Sandbox clusters are deleted after 30 days of inactivity.
- Ability to see the [version](../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) of YugabyteDB that your cluster is running on.

**Fixes**

- Windows and Firefox keyboard shortcuts work correctly in Cloud Shell.

### December 16, 2021

**New Features**

- Self service [Virtual Private Cloud (VPC) networking](../cloud-basics/cloud-vpcs/). Use VPC networks to lower network latencies and make your application and database infrastructure more secure. Create VPCs in AWS or GCP and peer them with application VPCs in the same cloud provider. VPC networking is managed on the **VPC Network** tab of the **Networking** page.
- Ability to [enable pre-bundled extensions](../cloud-clusters/add-extensions/) using the `CREATE EXTENSION` command. YugabyteDB includes [pre-bundled PostgreSQL extensions](../../explore/ysql-language-features/pg-extensions/) that are tested to work with YSQL. Admin users now have additional permissions to allow them to enable these extensions in databases. (If you need to install a database extension that is not pre-bundled, contact {{% support-cloud %}}
.)

### December 2, 2021

**New Features**

- Additional [performance metrics](../cloud-monitor/overview/). The new cluster **Performance Metrics** tab features new metrics including YSQL and YCQL operations per second, YSQL and YCQL latency, network bytes per second, and more. Use these metrics to ensure the cluster configuration matches its performance requirements.
- Ability to review running queries using the [Live Queries](../cloud-monitor/cloud-queries-live/) on the cluster **Performance** tab. Use this information to visually identify relevant database operations and evaluate query execution times.
- Ability to review slow YSQL queries using the [YSQL Slow Queries](../cloud-monitor/cloud-queries-slow/) on the cluster **Performance** tab. You can use this information to identify slower-running database operations, look at query execution times over time, and discover potential queries for tuning.

**Database**

- YugabyteDB updated to version 2.8. New clusters automatically use the new version. Existing clusters will be upgraded in the coming weeks.

### November 18, 2021

**New Features**

- Support for auditing account activity using the new **Activity** tab on the **Security** page. The tab provides a running audit of activity, including:

  - backups
  - cluster creation and modification
  - changes to account users and their roles
  - billing changes
  - allow list changes

- Support for multiple Admin users on your account, and Admin users can now change the role of existing users. You can also invite multiple users at once, and assign them a role (Developer or Admin) when you invite them. You manage users using the **Users** tab on the **Security** page.
- Additional charts on the **Invoices** on the **Usage & Billing** page, which break costs down by cluster and infrastructure (instance costs, storage, and data transfer) so that you can quickly evaluate your costs.

**Fixes**

- Developer users can now use Cloud Shell.

### October 5, 2021

**New Features**

- The [YugabyteDB Aeon Status](https://status.yugabyte.cloud/) page shows the current uptime status of YugabyteDB Aeon and the [Yugabyte Support Portal](https://support.yugabyte.com/), along with maintenance notices and incident reports.
- Ability to review cluster activity using the new cluster **Activity** tab.

### September 15, 2021

**New Features**

- Ability to [create clusters](../cloud-basics/create-clusters/) suitable for production workloads. YugabyteDB Aeon clusters support horizontal and vertical scaling, VPC peering, and scheduled and manual backups.
- Billing support. Set up a billing profile, manage payment methods, and review invoices on the [Billing](../cloud-admin/cloud-billing-profile) tab. (You must create a billing profile and add a payment method before you can create any clusters apart from your Sandbox cluster.)

### September 8, 2021

This release includes the following features:

- Sandbox clusters (one per account)
- AWS and GCP cloud support
- IP allow lists for network security
- Cloud Shell for running SQL queries from your browser
- YSQL and YCQL API support
- Multiple users - invite additional users
- Encryption at rest and in transit

## Known issues

- **Missing Slow Queries** - On clusters with multiple nodes, in some circumstances some nodes may not return all query statements when requested. If this happens, the query statements will be missing from the Slow Queries page.
- **Slow Queries Reset** - When resetting Slow Queries, the query used for the reset remains in the table.
- **Tables** - In some instances in Sandbox clusters, the **Tables** tab will show all tables with a size of 0B.
- **Clusters** - No support for scaling vCPUs on single node clusters.
- **Metrics** - The **View Full Screen** option in charts on the cluster **Overview** and **Performance Metrics** pages does not work in some versions of Safari 14.0 (Big Sur).
- **Metrics** - Some clusters in European regions may show occasional spikes in the YSQL Operations/sec chart. This is due to cluster health checks and can be ignored.
- **Metrics** - For a cluster with read replicas with different IOPS provisioned (AWS only), the provisioned IOPS metric shows the same IOPS across all replicas.
- **Widely-dispersed regions** - For multi-region clusters with widely-dispersed regions, Performance Advisor, Slow Queries, and some metrics may not return any results.
- **Maximum number of regions** - Multi-region clusters and their read replicas are limited to a maximum of 8 regions.

### Known issues in Cloud Shell

- If [Cloud Shell](../cloud-connect/connect-cloud-shell/) stops responding, close the browser tab and restart Cloud Shell.
- Occasionally, Cloud Shell will take longer than normal to load; subsequent loads will be faster.
