---
title: Before you begin
headerTitle: Before you begin
description: Obtain your cluster certificate and add your computer to the IP allow list.
headcontent: Using a cluster in YugabyteDB Managed
menu:
  v2.16:
    parent: build-apps
    name: Before you begin
    identifier: cloud-add-ip
    weight: 20
type: docs
---

The tutorials assume you have deployed a YugabyteDB cluster in YugabyteDB Managed or locally. Refer to [Quick start](../../../quick-start-yugabytedb-managed/).

In addition, **if you are using YugabyteDB Managed**, you need the following to run the sample applications:

- The cluster CA certificate
- Your computer added to the cluster IP allow list

{{< note title="Note" >}}

To take advantage of smart driver load balancing features when connecting to clusters in YugabyteDB Managed, applications using smart drivers must be deployed in a VPC that has been peered with the cluster VPC. For information on VPC networking in YugabyteDB Managed, refer to [VPC network](../../../yugabyte-cloud/cloud-basics/cloud-vpcs/).

For applications that access the cluster from a non-peered network, use the upstream PostgreSQL driver instead; in this case, the cluster performs the load balancing. Applications that use smart drivers from non-peered networks fall back to the upstream driver behaviour automatically.

{{< /note >}}

## Download your cluster certificate

YugabyteDB Managed uses TLS 1.2 for communicating with clusters, and digital certificates to verify the identity of clusters. The [cluster CA certificate](../../../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/) is used to verify the identity of the cluster when you connect to it from an application or client.

To download the certificate to the computer that will be connecting to the cluster, do the following:

![Download certificate and add IP](/images/yb-cloud/cloud-add-ip.gif)

1. In YugabyteDB Managed, select your cluster and click **Connect**.
1. Click **YugabyteDB Client Shell** or **Connect to your Application**.
1. Click **Download CA Cert** to download the cluster `root.crt` certificate to your computer.

## Add your computer to the cluster IP allow list

Access to YugabyteDB Managed clusters is limited to IP addresses that you explicitly allow using [IP allow lists](../../../yugabyte-cloud/cloud-secure-clusters/add-connections/). To enable applications to connect to your cluster, you need to add your computer's IP address to the cluster IP allow list.

To add your computer to the cluster IP allow list:

1. In YugabyteDB Managed, select your cluster.
1. Click **Add IP Allow List**.
1. Click **Create New List and Add to Cluster**.
1. Enter a name for the allow list.
1. Click **Detect and add my IP to this list** to add your own IP address.
1. Click **Save**.

The allow list takes up to 30 seconds to become active.
