---
title: Create a Sandbox cluster
linkTitle: Create a Sandbox cluster
description: Create a Sandbox cluster to get started using YugabyteDB Managed.
headcontent:
image: /images/section_icons/index/quick_start.png
menu:
  preview:
    identifier: qs-add
    parent: cloud-quickstart
    weight: 100
isTocNested: true
showAsideToc: true
---

The Sandbox cluster provides a fully functioning single node YugabyteDB cluster deployed to the region of your choice. The cluster is free forever and includes enough resources to explore the core features available for developing applications with YugabyteDB. No credit card information is required. You can only have one Sandbox cluster.

{{< youtube id="KZSrl12x1ew" title="Create your first YugabyteDB Managed cluster" >}}

After setting up your YugabyteDB Managed account, [log in](https://cloud.yugabyte.com/login) to access YugabyteDB Managed.

The first time you sign in, YugabyteDB Managed provides a guided tour to help you get started. Follow the tour to:

- create your free Sandbox cluster
- connect to your cluster and explore distributed SQL using the Cloud Shell tutorial
- explore a sample application

## Create your Sandbox cluster

To create your Sandbox cluster:

![Create a Sandbox cluster](/images/yb-cloud/cloud-add-free-cluster.gif)

1. On the welcome page, click **Create a Free cluster**.

1. Select **Sandbox** and click **Choose**.

1. Enter a name for the cluster, and choose the cloud provider (AWS or GCP), then click **Next**.

1. Choose the region in which to deploy the cluster, then click **Next**.

1. Click **Download credentials**. The default credentials are for a database user named "admin". You'll use these credentials when connecting to your YugabyteDB database.

1. Click **Create Cluster**.

After you complete the wizard, YugabyteDB Managed bootstraps and provisions the cluster, and configures YugabyteDB. The process takes around 5 minutes. While you wait, you can optionally fill out a survey to customize your getting started experience.

When the cluster is ready, the cluster [Overview](../../cloud-monitor/overview/) is displayed. You now have a fully configured YugabyteDB cluster provisioned in YugabyteDB Managed.

{{< note title="Sandbox clusters" >}}

YugabyteDB is a distributed database optimized for deployment across a cluster of servers. The Sandbox cluster has a single node and limited resources, suitable for running tutorials, [Yugabyte University](https://university.yugabyte.com), and [building sample applications](../cloud-build-apps/). To evaluate YugabyteDB Managed for production use or conduct a proof-of-concept (POC), contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431) for trial credits.

{{< /note >}}

### Learn more

[Differences between Sandbox and Dedicated clusters](../../cloud-faq/#what-are-the-differences-between-free-and-standard-clusters)

[Deploy production clusters](../../cloud-basics/)

[Authorize access to your cluster](../../cloud-secure-clusters/add-connections/)

## Next step

[Connect to your cluster](../qs-connect/) using the Cloud Shell.
