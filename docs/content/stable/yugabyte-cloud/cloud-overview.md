---
title: YugabyteDB Managed
linkTitle: YugabyteDB Managed
description: Fully managed YugabyteDB-as-a-Service
headcontent: Fully managed YugabyteDB-as-a-Service
image: /images/section_icons/index/icon-native-cloud.png
aliases:
  - /preview/yugabyte-cloud/
  - /preview/deploy/yugabyte-cloud/
menu:
  preview_yugabyte-cloud:
    identifier: cloud-overview
    parent: yugabytedb-managed
    weight: 1
type: indexpage
layout: list
---

YugabyteDB Managed is a fully managed YugabyteDB-as-a-Service that allows you to run YugabyteDB clusters on [Google Cloud Platform (GCP)](https://cloud.google.com/) and [Amazon Web Services (AWS)](https://aws.amazon.com/).

### Start here

To begin using YugabyteDB Managed, go to [Quick start](../cloud-quickstart/).

#### Additional resources

- Video playlist: [YugabyteDB Managed Getting Started](https://www.youtube.com/playlist?list=PL8Z3vt4qJTkJqisBVRDi6GAy8rhVo1xjc)

- Course at Yugabyte University: [YugabyteDB Managed Basics](https://university.yugabyte.com/courses/yugabytedb-managed-basics)

### Cluster fundamentals

{{< tabpane code=false >}}

  {{% tab header="Deploy" lang="deploy" %}}

**Plan your cluster**\
Decide on the topology, provider, region, and sizing for your cluster.

[Read more](../cloud-basics/create-clusters-overview/)

**Create a VPC network**\
Use VPC networks to lower network latencies, make your application and database infrastructure more secure, and reduce network data transfer costs.

[Read more](../cloud-basics/cloud-vpcs/)

**Deploy a cluster**\
Create single- and multi-region clusters in a variety of topologies.

[Read more](../cloud-basics/create-clusters/)

  {{% /tab %}}

  {{% tab header="Secure" lang="secure" %}}

**Authorize network access**\
Add inbound network access from clients to your cluster using IP allow lists.

[Read more](../cloud-secure-clusters/add-connections/)

**Download your cluster certificate**\
Download your cluster certificate for use when connecting to your database.

[Read more](../cloud-secure-clusters/cloud-authentication/)

**Add database users**\
Provide team members and applications access to the cluster's YugabyteDB database by adding them as users.

[Read more](../cloud-secure-clusters/add-users/)

**Audit activity**\
Review activity on your cloud, including cluster creation, changes to clusters, changes to IP allow lists, backup activity, and billing.

[Read more](../cloud-secure-clusters/cloud-activity/)

  {{% /tab %}}

  {{% tab header="Connect" lang="connect" %}}

**From your browser**\
Use Cloud Shell to connect to your database using any modern browser. No need to set up an IP allow list, all you need is your database password. Includes a built-in YSQL quick start guide.

[Read more](../cloud-connect/connect-cloud-shell/)

**From your desktop**\
Install the ysqlsh or ycqlsh client shells to connect to your database from your desktop. YugabyteDB Managed also supports psql and third-party tools such as pgAdmin.
Requires your computer to be added to the cluster IP allow list and an SSL connection.

[Read more](../cloud-connect/connect-client-shell/)

**From applications**\
Obtain the connection parameters needed to connect your application driver to your cluster database.
Requires the VPC or machine hosting the application to be added to the cluster IP allow list and an SSL connection.

[Read more](../cloud-connect/connect-applications/)

  {{% /tab %}}

  {{% tab header="Monitor" lang="monitor" %}}

**Manage alerts**\
Get notified when cluster and database resource usage exceeds predefined limits, or of potential billing issues.

[Read more](../cloud-monitor/cloud-alerts/)

**View performance metrics**\
Monitor database and cluster performance in real time.

[Read more](../cloud-monitor/overview/)

**Optimize performance**\
Scan your database for potential optimizations.

[Read more](../cloud-monitor/cloud-advisor/)

  {{% /tab %}}

  {{% tab header="Manage" lang="manage" %}}

**Scale clusters**\
Scale clusters vertically or horizontally as your requirements change.

[Read more](../cloud-clusters/configure-clusters/)

**Backup and restore**\
Configure a regular backup schedule, run manual backups, and review previous backups.

[Read more](../cloud-clusters/backup-clusters/)

**Pause, resume, and delete clusters**\
Pause or delete clusters to reduce costs.

[Read more](../cloud-clusters/)

**Schedule upgrades**\
Schedule a weekly maintenance window and set exclusion periods during which upgrades won't be done.

[Read more](../cloud-clusters/cloud-maintenance)

**Add extensions**\
Extend the functionality of your cluster using PostgreSQL extensions.

[Read more](../cloud-clusters/add-extensions)

  {{% /tab %}}

{{< /tabpane >}}

### Account management

- [Add users](../cloud-admin/manage-access/) - Invite team members to your YugabyteDB Managed account.
- [Manage billing](../cloud-admin/cloud-billing-profile/) - Create your billing profile, manage your payment methods, and review invoices.

### More information

- [What's new](../release-notes/) - See what's new in YugabyteDB Managed, what regions are supported, and known issues.
- [Troubleshooting](../cloud-troubleshoot/) - Get solutions to common problems. To check YugabyteDB Managed status, go to [status](https://status.yugabyte.cloud/).
- [FAQ](../../faq/yugabytedb-managed-faq/) - Get answers to frequently asked questions.
- [Security architecture](../cloud-security/) - Review YugabyteDB Managed's security architecture and shared responsibility model.
- [Cluster costs](../cloud-admin/cloud-billing-costs/) - Review how YugabyteDB Managed charges for clusters.
