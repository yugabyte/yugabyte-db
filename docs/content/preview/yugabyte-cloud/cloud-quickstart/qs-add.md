---
title: Create a free cluster
linkTitle: Create a free cluster
description: Create a Free cluster to get started using YugabyteDB Managed.
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

The free cluster provides a fully functioning single node YugabyteDB cluster deployed to the region of your choice. The cluster is free forever and includes enough resources to explore the core features available for developing applications with YugabyteDB. No credit card information is required. You can only have one free cluster.

After setting up your YugabyteDB Managed account, [log in](https://cloud.yugabyte.com/login) to access YugabyteDB Managed. YugabyteDB Managed has the following main sections, accessed via the left menu:

- **Getting Started** - Create your free cluster and access documentation.

- **Clusters** - Add, monitor, and manage clusters.

- **Alerts** - Configure cluster and billing alerts, and view notifications.

- **Network Access** - Set up VPC networks and authorize access to your clusters using IP allow lists.

- **Admin** - Manage billing and payment methods, add cloud users, and review cloud activity.

## Create your free cluster

To create your free cluster:

1. On the **Getting Started** page, click **Create a free cluster** to open the **Create Cluster** wizard.

1. Select **YugabyteDB Managed Free** and click **Next**.

1. Choose the cloud provider (AWS or GCP), enter a name for the cluster, and choose the region in which to deploy the cluster, then click **Next**.

1. Click **Download credentials**. The default credentials are for a database user named "admin". You'll use these credentials when connecting to your YugabyteDB database in the cloud.

1. Click **Create Cluster**.

After you complete the wizard, YugabyteDB Managed bootstraps and provisions the cluster, and configures YugabyteDB. The process takes up to 15 minutes.

When the cluster is ready, the cluster [Overview](../../cloud-monitor/overview/) is displayed. You now have a fully configured YugabyteDB cluster provisioned in YugabyteDB Managed.

{{< note title="Note" >}}

YugabyteDB is a distributed database optimized for deployment across a cluster of servers. The free cluster has a single node and limited resources, suitable for running tutorials, [Yugabyte University](https://university.yugabyte.com), and [building sample applications](../cloud-build-apps/). To evaluate YugabyteDB Managed for production use or conduct a proof-of-concept (POC), contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431) for trial credits.

{{< /note >}}

### Learn more

[Differences between free and standard clusters](../../cloud-faq/#what-are-the-differences-between-free-and-standard-clusters)

[Deploy production clusters](../../cloud-basics/)

[Authorize access to your cluster](../../cloud-secure-clusters/add-connections/)

## Next step

[Connect to your cluster](../qs-connect/) using the Cloud Shell.
