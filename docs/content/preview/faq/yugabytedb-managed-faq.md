---
title: YugabyteDB Aeon FAQ
linkTitle: YugabyteDB Aeon FAQ
description: YugabyteDB Aeon frequently asked questions.
aliases:
  - /preview/yugabyte-cloud/cloud-faq/
menu:
  preview_faq:
    identifier: yugabytedb-managed-faq
    parent: faq
    weight: 30
type: docs
unversioned: true
rightNav:
  hideH3: true
  hideH4: true
---

### Contents

##### YugabyteDB Aeon

- [What is YugabyteDB Aeon?](#what-is-yugabytedb-aeon)
- [How is YugabyteDB Aeon priced?](#how-is-yugabytedb-aeon-priced)
- [What regions are available?](#what-regions-are-available)

##### Clusters

- [What are the differences between Sandbox and Dedicated clusters?](#what-are-the-differences-between-sandbox-and-dedicated-clusters)
- [What can I do if I run out of resources on my Sandbox cluster?](#what-can-i-do-if-i-run-out-of-resources-on-my-sandbox-cluster)
- [Can I migrate my Sandbox to a Dedicated cluster?](#can-i-migrate-my-sandbox-to-a-dedicated-cluster)
- [What is the upgrade policy for clusters?](#what-is-the-upgrade-policy-for-clusters)

##### YugabyteDB

- [What version of YugabyteDB does my cluster run on?](#what-version-of-yugabytedb-does-my-cluster-run-on)
- [Can I test YugabyteDB locally?](#can-i-test-yugabytedb-locally)

##### Support

- [Is support included in the base price?](#is-support-included-in-the-base-price)
- [Where can I find the support policy and SLA?](#where-can-i-find-the-support-policy-and-sla)
- [How do I check the status of YugabyteDB Aeon?](#how-do-i-check-the-status-of-yugabytedb-aeon)

##### Security

- [How secure is my cluster?](#how-secure-is-my-cluster)

##### Cluster management

- [What cluster configurations can I create?](#what-cluster-configurations-can-i-create)
- [How do I connect to my cluster?](#how-do-i-connect-to-my-cluster)
- [Why is my Sandbox cluster paused?](#why-is-my-sandbox-cluster-paused)
- [How do I keep my Sandbox cluster from being paused or deleted?](#how-do-i-keep-my-sandbox-cluster-from-being-paused-or-deleted)
- [What qualifies as activity on a cluster?](#what-qualifies-as-activity-on-a-cluster)

##### Backups

- [How are clusters backed up?](#how-are-clusters-backed-up)
- [Can I download backups?](#can-i-download-backups)

## YugabyteDB Aeon

### What is YugabyteDB Aeon?

[YugabyteDB Aeon](https://www.yugabyte.com/blog/introducing-yugabytedb-aeon/) (previously known as YugabyteDB Managed) is a fully managed YugabyteDB-as-a-Service that allows you to run YugabyteDB clusters on Amazon Web Services (AWS), Microsoft Azure, and Google Cloud Platform (GCP).

You access your YugabyteDB Aeon clusters via [YSQL](../../api/ysql/) and [YCQL](../../api/ycql/) client APIs, and administer your clusters using the [YugabyteDB Aeon UI](https://cloud.yugabyte.com/login).

YugabyteDB Aeon runs on top of [YugabyteDB Anywhere](../../yugabyte-platform/).

### How is YugabyteDB Aeon priced?

Yugabyte bills for its services as follows:

- Charges by the minute for your YugabyteDB Aeon clusters, based on your pricing plan.
- Tabulates costs daily.
- Displays your current monthly costs under **Invoices** on the **Usage & Billing** tab.

For information on YugabyteDB Aeon pricing, refer to [YugabyteDB Pricing](https://www.yugabyte.com/pricing/). For a description of how cluster configurations are costed, refer to [Cluster costs](../../yugabyte-cloud/cloud-admin/cloud-billing-costs/).

### What regions are available?

Refer to [Cloud provider regions](../../yugabyte-cloud/cloud-basics/create-clusters-overview/#cloud-provider-regions) for a list currently supported regions.

YugabyteDB Aeon supports all the regions that have robust infrastructure and sufficient demand from customers. If there are regions you would like added, reach out to {{% support-cloud %}}.

## Clusters

### What are the differences between Sandbox and Dedicated clusters?

Use the free Sandbox cluster to get started with YugabyteDB. The Sandbox cluster is limited to a single node and 10GB of storage. Although not suitable for production workloads or performance testing, the cluster includes enough resources to start exploring the core features available for developing applications with YugabyteDB. Sandbox clusters are provisioned with a [preview release](#what-version-of-yugabytedb-does-my-cluster-run-on). You can only have one Sandbox cluster. Sandbox clusters that are inactive for 10 days are [paused](#why-is-my-sandbox-cluster-paused); after 15 days they are deleted.

Dedicated clusters can have unlimited nodes and storage and are suitable for production workloads. They also support horizontal and vertical scaling - nodes and storage can be added or removed to suit your production loads. Dedicated clusters also support VPC networking, and scheduled and manual backups. By default, Dedicated clusters are provisioned using a [stable release](#what-version-of-yugabytedb-does-my-cluster-run-on).

A YugabyteDB Aeon account is limited to a single Sandbox cluster; you can add as many Dedicated clusters as you need.

| Feature | Sandbox | Dedicated |
| :------ | :------ | :-------- |
| Cluster | Single Node | Any |
| vCPU/Storage | Up to 2 vCPU / 4 GB Memory / 10 GB storage | Any |
| [Regions](../../yugabyte-cloud/cloud-basics/create-clusters-overview/#cloud-provider-regions) | All | All |
| Upgrades | Automatic | Automatic with customizable [maintenance windows](../../yugabyte-cloud/cloud-clusters/cloud-maintenance/) |
| [VPC networking](../../yugabyte-cloud/cloud-basics/cloud-vpcs/) | No | Yes |
| [Fault tolerance](../../yugabyte-cloud/cloud-basics/create-clusters-overview/#fault-tolerance) | None (Single node, RF-1) | Multi node RF-3 clusters with region, availability zone, and node level |
| [Connections](../../yugabyte-cloud/cloud-basics/create-clusters-overview/#sizing) | Up to 15 simultaneous connections | 15 per vCPU per node |
| [Scaling](../../yugabyte-cloud/cloud-clusters/configure-clusters/) | None | Horizontal and Vertical |
| [Backups](../../yugabyte-cloud/cloud-clusters/backup-clusters/) | None | Scheduled and on-demand |
| [YugabyteDB version](#what-version-of-yugabytedb-does-my-cluster-run-on) | Innovation<br>Preview<br>Early Access | Production<br>Innovation<br>Early Access |
| Price | Free | [Pay-as-you-go and subscription](#how-is-yugabytedb-aeon-priced)<br>[Free trial available](../../yugabyte-cloud/managed-freetrial/) |
| Support | Slack Community | Enterprise Support |

### What can I do if I run out of resources on my Sandbox cluster?

If you want to continue testing YugabyteDB with more resource-intensive scenarios, you can:

- [Request a free trial](../../yugabyte-cloud/managed-freetrial/) to try out bigger clusters with more resources.
- Download and run YugabyteDB on a local machine. For instructions, refer to [Quick Start](../../quick-start/).
- [Add a payment method](../../yugabyte-cloud/cloud-admin/cloud-billing-profile/) to upgrade to a fault-tolerant [single- or multi-region cluster](../../yugabyte-cloud/cloud-basics/create-clusters-topology/).

### Can I migrate my Sandbox to a Dedicated cluster?

Currently self-service migration is not supported. Contact {{% support-cloud %}} for help with migration.

### What is the upgrade policy for clusters?

Upgrades are automatically handled by Yugabyte. There are two types of upgrades:

#### YugabyteDB Aeon

During a [maintenance window](../../yugabyte-cloud/cloud-clusters/cloud-maintenance/), YugabyteDB Aeon may be in read-only mode and not allow any edit changes. The upgrade has no impact on running clusters. Yugabyte will notify you in advance of the maintenance schedule.

#### Cluster (YugabyteDB) version upgrade

To keep up with the latest bug fixes, improvements, and security fixes, Yugabyte upgrades your cluster database to the [latest version](#what-version-of-yugabytedb-does-my-cluster-run-on). The database is upgraded to the latest release in the [release track](#what-version-of-yugabytedb-does-my-cluster-run-on) that was selected when the cluster was created.

Yugabyte only upgrades clusters during scheduled maintenance windows. Yugabyte notifies you in advance of any upcoming upgrade via email.

Updates to fault-tolerant clusters are done on a rolling basis to avoid any downtime.

For more information, refer to [Maintenance windows](../../yugabyte-cloud/cloud-clusters/cloud-maintenance/).

## YugabyteDB

### What version of YugabyteDB does my cluster run on?

Dedicated clusters are provisioned with a **stable** release, from a YugabyteDB [stable release](/preview/releases/versioning/#release-versioning-convention-for-stable-releases) series. When creating a dedicated cluster, you can choose one of the following tracks:

- Production - Has less frequent updates, using select stable builds that have been tested longer in YugabyteDB Aeon.
- Innovation - Updated more frequently, providing quicker access to new features.
- Early Access - Updated more frequently, providing access to the most recent stable YugabyteDB release.

In addition to the Innovation and Early Access tracks, Sandbox clusters can be provisioned with a **preview** release, from the YugabyteDB [preview release](/preview/releases/versioning/#release-versioning-convention-for-preview-releases) series.

Once a cluster is created, it is upgraded with releases from the track that was assigned at creation.

To view the database version running on a particular cluster, navigate to the **Clusters** page; the database version is displayed next to the cluster name; hover over the version to see the release track.

### Can I test YugabyteDB locally?

To test locally, download and install YugabyteDB on a local machine. Refer to [Quick Start](../../quick-start/). For accurate comparison with cloud, be sure to download the version that is running on YugabyteDB Aeon.

## Support

### Is support included in the base price?

Enterprise Support is included in the base price for Dedicated clusters. Refer to the [YugabyteDB Aeon Support Services Terms and Conditions](https://www.yugabyte.com/yugabyte-cloud-support-services-terms-and-conditions/).

Sandbox and Dedicated cluster customers can also use the [YugabyteDB Slack community]({{<slack-invite>}}).

### Where can I find the support policy and SLA?

The YugabyteDB Aeon Service Level Agreement (SLA), terms of service, acceptable use policy, and more can be found on the [Yugabyte Legal](https://www.yugabyte.com/legal/) page.

### How do I check the status of YugabyteDB Aeon?

The [YugabyteDB Aeon Status](https://status.yugabyte.cloud/) page displays the current uptime status of YugabyteDB Aeon, customer clusters, and the [Yugabyte Support Portal](https://support.yugabyte.com/).

The status page also provides notices of scheduled maintenance, current incidents and incident history, and historical uptime.

Subscribe to the status page by clicking **Subscribe to Updates**. Email notifications are sent when incidents are created, updated, and resolved.

## Security

### How secure is my cluster?

Your data is processed at the YugabyteDB Aeon account level, and each account is a single tenant, meaning it runs its components for only one customer. Clusters in your account are isolated from each other in a separate VPC, and access is limited to the IP addresses you specify in allow lists assigned to each cluster. Resources are not shared between clusters.

YugabyteDB Aeon uses both encryption in transit and encryption at rest to protect clusters and cloud infrastructure. YugabyteDB Aeon also provides DDoS and application layer protection, and automatically blocks network protocol and volumetric DDoS attacks.

YugabyteDB Aeon uses a shared responsibility model for security. For more information on YugabyteDB Aeon security, refer to [Security architecture](../../yugabyte-cloud/cloud-security/).

## Cluster management

### What cluster configurations can I create?

Using YugabyteDB Aeon, you can create single- and multi-region clusters that can be deployed across multiple and single availability zones.

The Fault Tolerance of a cluster determines how resilient the cluster is to failures and, by extension, the cluster configuration. You can configure clusters with the following fault tolerances in YugabyteDB Aeon:

- **Region Level** - a minimum of 3 nodes spread across 3 regions with a [replication factor](../../architecture/docdb-replication/replication/) (RF) of 3. YugabyteDB can continue to do reads and writes even in case of an region failure. This configuration provides the maximum protection for a region failure. For horizontal scaling, nodes are scaled in increments of 3.
- **Availability Zone Level** - a minimum of 3 nodes spread across multiple availability zones with a RF of 3. YugabyteDB can continue to do reads and writes even in case of an availability zone failure. This configuration provides the protection for a data center failure. For horizontal scaling, nodes are scaled in increments of 3.
- **Node Level** - a minimum of 3 nodes deployed in a single availability zone with a RF of 3. YugabyteDB can continue to do reads and writes even in case of a node failure, but this configuration is not resilient to availability zone outages. For horizontal scaling, you can scale nodes in increments of 1.
- **None** - single node, with no replication or resiliency. Recommended for development and testing only.

For production clusters, a minimum of Availability Zone Level is recommended. Whether you choose Region or Availability Zone Level depends on your application architecture, design, and latency requirements.

For multi-region deployments, you can deploy a variety of topologies, including synchronously replicated, geo-level partitioned, and read replicas. For more information, refer to [Topologies](../../yugabyte-cloud/cloud-basics/create-clusters-topology/).

Sandbox clusters are limited to a single node in a single region.

### How do I connect to my cluster?

You can connect to clusters in the following ways:

{{< tabpane text=true >}}

  {{% tab header="Cloud Shell" lang="Cloud Shell" %}}

Run the [ysqlsh](../../admin/ysqlsh/) or [ycqlsh](../../admin/ycqlsh/) shell from your browser to connect to and interact with your YugabyteDB database. Cloud Shell does not require a CA certificate or any special network access configured.

When you connect using Cloud Shell with the YSQL API, the shell window also incorporates a [Quick Start Guide](../../yugabyte-cloud/cloud-quickstart/), with a series of pre-built queries for you to run.

  {{% /tab %}}

  {{% tab header="Client Shell" lang="Client Shell" %}}

Connect to your YugabyteDB cluster using the YugabyteDB [ysqlsh](../../admin/ysqlsh/) and [ycqlsh](../../admin/ycqlsh/) client shells installed on your computer.

Before you can connect using a client shell, you need to add your computer to the cluster IP allow list. Refer to [Assign IP Allow Lists](../../yugabyte-cloud/cloud-secure-clusters/add-connections/).

  {{% /tab %}}

  {{% tab header="psql" lang="psql" %}}

Because YugabyteDB is PostgreSQL-compatible, you can use [psql](https://www.postgresql.org/docs/current/app-psql.html) to connect to your clusters. The connection string to use is similar to what you would use for `ysqlsh`, as follows:

```sh
psql --host=<HOST_ADDRESS> --port=5433 --username=<DB USER> \
--dbname=yugabyte \
--set=sslmode=verify-full \
--set=sslrootcert=<ROOT_CERT_PATH>
```

For detailed steps for configuring other popular third party tools, see [Third party tools](../../tools/).

  {{% /tab %}}

  {{% tab header="Applications" lang="Applications" %}}

Applications connect to and interact with YugabyteDB using API client libraries (also known as client drivers). Before you can connect an application, you need to install the correct driver and configure it with the required connection parameters. You can also connect to YugabyteDB Aeon clusters using smart drivers.

For information on drivers supported by YugabyteDB, refer to [Drivers and ORMs](../../drivers-orms/). For sample applications using popular drivers, refer to [Build an application](../../tutorials/build-apps/).

For information on obtaining the connection parameters for your cluster, refer to [Connect applications](../../yugabyte-cloud/cloud-connect/connect-applications/).

Clusters have SSL ([encryption in-transit](../../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/)) enabled so make sure your driver details include SSL parameters.

Before you can connect, your application has to be able to reach your YugabyteDB Aeon. To add inbound network access from your application environment to YugabyteDB Aeon, add the public IP addresses to the [cluster IP allow list](../../yugabyte-cloud/cloud-secure-clusters/add-connections/), or use [VPC networking](../../yugabyte-cloud/cloud-basics/cloud-vpcs/) to add private IP addresses.

  {{% /tab %}}

{{< /tabpane >}}

For more details, refer to [Connect to clusters](../../yugabyte-cloud/cloud-connect/).

### Why is my Sandbox cluster paused?

Sandbox clusters are paused after 10 days of [inactivity](#what-qualifies-as-activity-on-a-cluster).

For more details, refer to [Inactive Sandbox clusters](../../yugabyte-cloud/cloud-basics/create-clusters/create-clusters-free/#inactive-sandbox-clusters).

### How do I keep my Sandbox cluster from being paused or deleted?

Sandbox clusters are paused after 10 days of inactivity. To keep a cluster from being paused, perform an action as described in [What qualifies as activity on a cluster?](#what-qualifies-as-activity-on-a-cluster)

To keep a paused cluster from being deleted, sign in to YugabyteDB Aeon, select the cluster on the **Clusters** page, and click **Resume**.

### What qualifies as activity on a cluster?

Sandbox clusters are paused after 10 days of inactivity. To keep your cluster from being paused, you (or, where applicable, an application connected to the database) can perform any of the following actions:

- Any SELECT, UPDATE, INSERT, or DELETE database operation.

- Create or delete tables.

- Add or remove IP allow lists.

- If the cluster is already paused, resume the cluster by signing in to YugabyteDB Aeon, selecting the cluster on the **Clusters** page, and clicking **Resume**.

## Backups

### How are clusters backed up?

By default, every cluster is backed up automatically every 24 hours, and these automatic backups are retained for 8 days. The first automatic backup is triggered 24 hours after creating a table, and is scheduled every 24 hours thereafter. You can change the default backup intervals by adjusting the backup policy settings.

YugabyteDB Aeon runs full backups, not incremental.

Backups are retained in the same region as the cluster.

Backups for AWS clusters are encrypted using AWS S3 server-side encryption. Backups for GCP clusters are encrypted using Google-managed server-side encryption keys. Backups for Azure clusters are encrypted using Azure-managed server-side encryption keys and client-side encryption is done using [GCM mode with AES](https://learn.microsoft.com/en-us/azure/storage/common/storage-service-encryption#client-side-encryption-for-blobs-and-queues).

Currently, YugabyteDB Aeon does not support backups of Sandbox clusters.

### Can I download backups?

Currently, YugabyteDB Aeon does not support self-service backup downloads. Contact {{% support-cloud %}} for assistance.
