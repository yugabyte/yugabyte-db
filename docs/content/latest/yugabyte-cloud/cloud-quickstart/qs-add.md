---
title: Add a free cluster
linkTitle: Add a free cluster
description: Add a Free cluster to get started using Yugabyte Cloud.
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

The quickest way to get started with Yugabyte Cloud is to set up a free cluster. Although not suitable for production workloads, the free cluster includes enough resources to learn and develop applications with YugabyteDB.

After setting up your Yugabyte Cloud account, [log in](https://cloud.yugabyte.com/login) to display the Yugabyte Cloud console. The console has the following main sections, accessed via the left menu:

- **Getting Started** - Create your free cluster and access documentation.

- [Clusters](../../cloud-clusters/) - Add, monitor, and manage clusters. 

- [Network Access](../../cloud-network) - Authorize access to your clusters using IP allow lists.

- [Admin](../../cloud-admin/) - Manage billing and payment methods and cloud users.

Use the **Help** icon in the top right corner to access documentation and support.

Use the **Profile** icon in the top right corner to sign out and change your user profile. To view your profile, click the profile icon and choose **Profile**. You can update your display name and password.

## Add a free cluster

To add a free cluster:

1. On the **Getting Started** page, click **Create a free cluster** to open the **Create Cluster** wizard. 

1. Select **Yugabyte Cloud Free** and click **Next**.

1. Choose the provider (AWS or GCP), enter a name for the cluster, and choose the region, then click **Next**.

1. Choose the credentials you'll use to connect to your YugabyteDB database in the cloud. You can choose the default set with a database user named "admin", or create your own.

1. Click **Download credentials** and save your credentials in a secure location.

1. Verify that your credentials are downloaded, and click **Create Cluster**.

Once you complete the wizard, Yugabyte Cloud bootstraps and provisions the cluster, and configures YugabyteDB. The process takes up to 15 minutes.

Once the cluster is ready, the cluster [Overview](../../cloud-clusters/overview) is displayed. You now have a fully configured YugabyteDB cluster provisioned in Yugabyte Cloud.

## Next step

- [Connect to your cluster](../qs-connect) using the Cloud Shell.
