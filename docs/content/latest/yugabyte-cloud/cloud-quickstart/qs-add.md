---
title: Create a free cluster
linkTitle: Create a free cluster
description: Create a Free cluster to get started using Yugabyte Cloud.
headcontent:
image: /images/section_icons/index/quick_start.png
menu:
  latest:
    identifier: qs-add
    parent: cloud-quickstart
    weight: 100
isTocNested: true
showAsideToc: true
---

The free cluster provides a fully functioning single node YugabyteDB cluster deployed to the region of your choice. The cluster is free forever and includes enough resources to explore the core features available for developing applications with YugabyteDB. No credit card information is required. You can only have one free cluster.

To create a free cluster:

1. After setting up your Yugabyte Cloud account, [log in](https://cloud.yugabyte.com/login) to display the Yugabyte Cloud console. The console has the following main sections, accessed via the left menu:

    - **Getting Started** - Create your free cluster and access documentation.

    - **Clusters** - Add, monitor, and manage clusters.

    - **Network Access** - Set up VPC networks and authorize access to your clusters using IP allow lists.

    - **Admin** - Manage billing and payment methods, add cloud users, and review cloud activity.

1. On the **Getting Started** page, click **Create a free cluster** to open the **Create Cluster** wizard.

1. Select **Yugabyte Cloud Free** and click **Next**.

1. Choose the provider (AWS or GCP), enter a name for the cluster, and choose the region, then click **Next**.

1. Choose the credentials you'll use to connect to your YugabyteDB database in the cloud. You can choose the default set with a database user named "admin", or create your own.

1. Click **Download credentials** and save your credentials in a secure location.

1. Verify that your credentials are downloaded, and click **Create Cluster**.

Once you complete the wizard, Yugabyte Cloud bootstraps and provisions the cluster, and configures YugabyteDB. The process takes up to 15 minutes.

Once the cluster is ready, the cluster [Overview](../../cloud-monitor/overview/) is displayed. You now have a fully configured YugabyteDB cluster provisioned in Yugabyte Cloud.

### Learn more

[Differences between free and standard clusters](../../cloud-faq/#what-are-the-differences-between-free-and-standard-clusters)

## Next step

[Connect to your cluster](../qs-connect/) using the Cloud Shell.
