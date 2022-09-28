---
title: YugabyteDB Managed FAQ
linkTitle: YugabyteDB Managed FAQ
description: YugabyteDB Managed frequently asked questions.
headcontent:
image: /images/section_icons/index/quick_start.png
aliases:
  - /preview/yugabyte-cloud/cloud-faq/
menu:
  preview_faq:
    identifier: yugabytedb-managed-faq
    parent: faq
    weight: 2775
type: docs
rightNav:
  hideH4: true
---

## YugabyteDB Managed

### What is YugabyteDB Managed?

YugabyteDB Managed is a fully managed YugabyteDB-as-a-Service that allows you to run YugabyteDB clusters on Google Cloud Platform (GCP) and Amazon Web Services (AWS).

You access your YugabyteDB Managed clusters via [YSQL](../../api/ysql/) and [YCQL](../../api/ycql/) client APIs, and administer your clusters using [YugabyteDB Managed](https://cloud.yugabyte.com/login).

See also [YugabyteDB Managed](https://www.yugabyte.com/cloud/) at yugabyte.com.

YugabyteDB Managed runs on top of [YugabyteDB Anywhere](../../yugabyte-platform/).

### How is YugabyteDB Managed priced?

Yugabyte bills for its services as follows:

- Charges by the minute for your YugabyteDB Managed clusters.
- Tabulates costs daily.
- Displays your current monthly costs under **Invoices** on the **Billing** tab.

For information on YugabyteDB Managed pricing, refer to the [YugabyteDB Managed Standard Price List](https://www.yugabyte.com/yugabyte-cloud-standard-price-list/). For a description of how cluster configurations are costed, refer to [Cluster costs](../../yugabyte-cloud/cloud-admin/cloud-billing-costs/).

### What regions in AWS and GCP are available?

Refer to [Cloud provider regions](../../yugabyte-cloud/release-notes/#cloud-provider-regions) for a list currently supported regions.

YugabyteDB Managed supports all the regions that have robust infrastructure and sufficient demand from customers. If there are regions you would like added, reach out to {{% support-cloud %}}.

## Clusters

### What are the differences between Sandbox and Dedicated clusters?

Use the free Sandbox cluster to get started with YugabyteDB. The Sandbox cluster is limited to a single node and 10GB of storage. Although not suitable for production workloads or performance testing, the cluster includes enough resources to start exploring the core features available for developing applications with YugabyteDB. Sandbox clusters are provisioned with a [preview release](#what-version-of-yugabytedb-does-my-cluster-run-on). You can only have one Sandbox cluster. Sandbox clusters that are inactive for 21 days are [paused](#why-is-my-sandbox-cluster-paused); after 30 days they are deleted.

Dedicated clusters can have unlimited nodes and storage and are suitable for production workloads. They also support horizontal and vertical scaling - nodes and storage can be added or removed to suit your production loads. Dedicated clusters also support VPC peering, and scheduled and manual backups. By default, Dedicated clusters are provisioned using a [stable release](#what-version-of-yugabytedb-does-my-cluster-run-on).

A YugabyteDB Managed account is limited to a single Sandbox cluster; you can add as many Dedicated clusters as you need.

| Feature | Sandbox | Dedicated |
| :----------- | :---------- | :---------- |
| Cluster | Single Node | Any |
| vCPU/Storage | Up to 2 vCPU / 4 GB Memory / 10 GB storage | Any |
| [Regions](../../yugabyte-cloud/release-notes/#cloud-provider-regions) | All | All |
| Upgrades | Automatic | Automatic with customizable [maintenance windows](../../yugabyte-cloud/cloud-clusters/cloud-maintenance/) |
| [VPC Peering](../../yugabyte-cloud/cloud-basics/cloud-vpcs/) | No | Yes |
| Fault Tolerance | None (Single node, RF-1) | Multi node RF-3 clusters with Availability zone and Node level |
| Connections | Up to 10 simultaneous connections | 10 per vCPU per node |
| [Scaling](../../yugabyte-cloud/cloud-clusters/configure-clusters/) | None | Horizontal and Vertical |
| [Backups](../../yugabyte-cloud/cloud-clusters/backup-clusters/) | None | Scheduled and on-demand |
| [YugabyteDB version](#what-version-of-yugabytedb-does-my-cluster-run-on) | Preview | Stable |
| Price | Free | [Pay-as-you-go and subscription](#how-is-yugabytedb-managed-priced) |
| Support | Slack Community | Enterprise Support |

### What can I do if I run out of resources on my Sandbox cluster?

If you want to continue testing YugabyteDB with more resource-intensive scenarios, you can:

- Download and run YugabyteDB on a local machine. For instructions, refer to [Quick Start](../../quick-start/).
- Upgrade to a fault tolerant [single- or multi-region cluster](../../yugabyte-cloud/cloud-basics/create-clusters/) to access bigger clusters with more resources.

To evaluate YugabyteDB Managed for production use or conduct a proof-of-concept (POC), contact {{% support-cloud %}} for trial credits.

### Can I migrate my Sandbox to a Dedicated cluster?

Currently self-service migration is not supported. Contact {{% support-cloud %}} for help with migration.

### What is the upgrade policy for clusters?

Upgrades are automatically handled by Yugabyte. There are two types of upgrades:

#### YugabyteDB Managed

During a [maintenance window](../../yugabyte-cloud/cloud-clusters/cloud-maintenance/), YugabyteDB Managed may be in read-only mode and not allow any edit changes. The upgrade has no impact on running clusters. Yugabyte will notify you in advance of the maintenance schedule.

#### Cluster (YugabyteDB) version upgrade

To keep up with the latest bug fixes, improvements, and security fixes, Yugabyte upgrades your cluster database to the [latest version](#what-version-of-yugabytedb-does-my-cluster-run-on).

Yugabyte only upgrades clusters during scheduled maintenance windows. Yugabyte notifies you in advance of any upcoming upgrade via email. The email includes the date and time of the maintenance window. An Upcoming Maintenance badge is also displayed on the cluster. You can start the upgrade any time by signing in to YugabyteDB Managed, selecting the cluster, clicking the **Upcoming Maintenance** badge, and clicking **Upgrade Now**. To delay the maintenance, click **Delay to next available window**. To manage maintenance windows, select the cluster [Maintenance tab](../../yugabyte-cloud/cloud-clusters/cloud-maintenance/).

The database is upgraded to the latest release in the [release track](#what-version-of-yugabytedb-does-my-cluster-run-on) that was selected when the cluster was created (either preview or stable). Sandbox clusters are always in the preview track.

Database upgrades of high-availability (multi-node) clusters are done on a rolling basis to avoid any downtime.

## YugabyteDB

### What version of YugabyteDB does my cluster run on?

Sandbox clusters are provisioned with a **preview** release, from the YugabyteDB [preview release](../../releases/release-notes/preview-release/) series.

By default, new Dedicated clusters are provisioned with a **stable** release, from the YugabyteDB [stable release](../../releases/release-notes/stable-release/) series. You can choose the preview track when you create the cluster.

Once a cluster is created, it is upgraded with releases from the release track that was assigned at creation (that is, either preview or stable).

To view the database version running on a particular cluster, navigate to the **Clusters** page; the database version is displayed next to the cluster name; hover over the version to see the release track.

### Can I test YugabyteDB locally?

To test locally, [download](https://download.yugabyte.com) and install YugabyteDB on a local machine. Refer to [Quick Start](../../quick-start/). For accurate comparison with cloud, be sure to download the version that is running on YugabyteDB Managed.

## Support

### Is support included in the base price?

Enterprise Support is included in the base price for Dedicated clusters. Refer to the [YugabyteDB Managed Support Services Terms and Conditions](https://www.yugabyte.com/yugabyte-cloud-support-services-terms-and-conditions/).

Sandbox and Dedicated cluster customers can also use the [YugabyteDB Slack community]({{<slack-invite>}}).

### Where can I find the support policy and SLA?

The YugabyteDB Managed Service Level Agreement (SLA), terms of service, acceptable use policy, and more can be found on the [Yugabyte Legal](https://www.yugabyte.com/legal/) page.

### How do I check the status of YugabyteDB Managed?

The [YugabyteDB Managed Status](https://status.yugabyte.cloud/) page displays the current uptime status of YugabyteDB Managed, customer clusters, and the [Yugabyte Support Portal](https://support.yugabyte.com/).

The status page also provides notices of scheduled maintenance, current incidents and incident history, and historical uptime.

Subscribe to the status page by clicking **Subscribe to Updates**. Email notifications are sent when incidents are created, updated, and resolved.

## Security

### How secure is my cluster?

Your data is processed at the YugabyteDB Managed account level, and each account is a single tenant, meaning it runs its components for only one customer. Clusters in your account are isolated from each other in a separate VPC, and access is limited to the IP addresses you specify in allow lists assigned to each cluster. Resources are not shared between clusters.

YugabyteDB Managed uses both encryption in transit and encryption at rest to protect clusters and cloud infrastructure. YugabyteDB Managed also provides DDoS and application layer protection, and automatically blocks network protocol and volumetric DDoS attacks.

YugabyteDB Managed uses a shared responsibility model for security. For more information on YugabyteDB Managed security, refer to [Security architecture](../../yugabyte-cloud/cloud-security/).

## Cluster management

### What cluster configurations can I create?

Using YugabyteDB Managed, you can create single region clusters that can be deployed across multiple and single availability zones.

The Fault Tolerance of a cluster determines how resilient the cluster is to node and availability zone failures and, by extension, the cluster configuration. You can configure clusters with the following fault tolerances in YugabyteDB Managed:

- **Availability Zone Level** - a minimum of 3 nodes spread across multiple availability zones with a RF of 3. YugabyteDB can continue to do reads and writes even in case of an availability zone failure. This configuration provides the maximum protection for a data center failure. Recommended for production deployments. For horizontal scaling, nodes are scaled in increments of 3.
- **Node Level** - a minimum of 3 nodes deployed in a single availability zone with a [replication factor](../../architecture/docdb-replication/replication/) (RF) of 3. YugabyteDB can continue to do reads and writes even in case of a node failure, but this configuration is not resilient to availability zone outages. For horizontal scaling, you can scale nodes in increments of 1.
- **None** - single node, with no replication or resiliency. Recommended for development and testing only.

For multi-region deployments, you can deploy a variety of topologies, including synchronously replicated, geo-level partitioned, cross-cluster, and read replicas. For more information, refer to [Topologies](../../yugabyte-cloud/cloud-basics/create-clusters-topology/).

Sandbox clusters are limited to a single node in a single region.

### How do I connect to my cluster?

You can connect to clusters in the following ways:

#### Cloud Shell

Run the [ysqlsh](../../admin/ysqlsh/) or [ycqlsh](../../admin/ycqlsh/) shell from your browser to connect to and interact with your YugabyteDB database. Cloud Shell does not require a CA certificate or any special network access configured. When you connect using Cloud Shell with the YSQL API, the shell window also incorporates a [Quick Start Guide](../../quick-start-yugabytedb-managed/), with a series of pre-built queries for you to run.

#### Client Shell

Connect to your YugabyteDB cluster using the YugabyteDB [ysqlsh](../../admin/ysqlsh/) and [ycqlsh](../../admin/ycqlsh/) client shells installed on your computer.

Before you can connect using a client shell, you need to add your computer to the cluster IP allow list. Refer to [Assign IP Allow Lists](../../yugabyte-cloud/cloud-secure-clusters/add-connections/).

You must be running the latest versions of the client shells (Yugabyte Client 2.6 or later), which you can download using the following command on Linux or macOS:

```sh
$ curl -sSL https://downloads.yugabyte.com/get_clients.sh | bash
```

Windows client shells require Docker:

```sh
docker run -it yugabytedb/yugabyte-client ysqlsh -h <hostname> -p <port>
```

#### psql

Because YugabyteDB is PostgreSQL-compatible, you can use [psql](https://www.postgresql.org/docs/current/app-psql.html) to connect to your clusters. The connection string to use is similar to what you would use for `ysqlsh`, as follows:

```sh
psql --host=<HOST_ADDRESS> --port=5433 --username=<DB USER> \
--dbname=yugabyte \
--set=sslmode=verify-full \
--set=sslrootcert=<ROOT_CERT_PATH>
```

For detailed steps for configuring other popular third party tools, see [Third party tools](../../tools/).

#### Applications

Applications connect to and interact with YugabyteDB using API client libraries (also known as client drivers). Before you can connect an application, you need to install the correct driver. Clusters have SSL (encryption in-transit) enabled so make sure your driver details include SSL parameters. To build sample applications using popular drivers, refer to [Build an application](../../develop/build-apps/).

Before you can connect, your application has to be able to reach your YugabyteDB Managed. To add inbound network access from your application environment to YugabyteDB Managed, add the public IP addresses to the [cluster IP access list](../../yugabyte-cloud/cloud-secure-clusters/add-connections/), or use [VPC peering](../../yugabyte-cloud/cloud-basics/cloud-vpcs/) to add private IP addresses.

For more details, refer to [Connect to clusters](../../yugabyte-cloud/cloud-connect/).

### Why is my Sandbox cluster paused?

Sandbox clusters are paused after 21 days of [inactivity](#what-qualifies-as-activity-on-a-cluster).

For more details, refer to [Inactive Sandbox clusters](../../yugabyte-cloud/cloud-basics/create-clusters/create-clusters-free/#inactive-sandbox-clusters).

### How do I keep my Sandbox cluster from being paused or deleted?

Sandbox clusters are paused after 21 days of inactivity. To keep a cluster from being paused, perform an action as described in [What qualifies as activity on a cluster?](#what-qualifies-as-activity-on-a-cluster)

To keep a paused cluster from being deleted, sign in to YugabyteDB Managed, select the cluster on the **Clusters** page, and click **Resume**.

### What qualifies as activity on a cluster?

Sandbox clusters are paused after 21 days of inactivity. To keep your cluster from being paused, you (or, where applicable, an application connected to the database) can perform any of the following actions:

- Any SELECT, UPDATE, INSERT, or DELETE database operation.

- Create or delete tables.

- Add or remove IP allow lists.

- If the cluster is already paused, resume the cluster by signing in to YugabyteDB Managed, selecting the cluster on the **Clusters** page, and clicking **Resume**.

## Backups

### How are clusters backed up?

By default, every cluster is backed up automatically every 24 hours, and these automatic backups are retained for 8 days. The first automatic backup is triggered 24 hours after creating a table, and is scheduled every 24 hours thereafter. You can change the default backup intervals by adjusting the backup policy settings.

YugabyteDB Managed runs full backups, not incremental.

Backups are retained in the same region as the cluster.

Backups for AWS clusters are encrypted using AWS S3 server-side encryption. Backups for GCP clusters are encrypted using Google-managed server-side encryption keys.

Currently, YugabyteDB Managed does not support backups of Sandbox clusters.

### Can I download backups?

Currently, YugabyteDB Managed does not support self-service backup downloads. Contact {{% support-cloud %}} for assistance.
