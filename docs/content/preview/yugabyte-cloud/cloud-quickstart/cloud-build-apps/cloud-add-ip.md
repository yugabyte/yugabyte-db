---
title: Before you begin
headerTitle: Before you begin
description: Obtain your cluster certificate and add your computer to the IP allow list.
menu:
  preview:
    parent: cloud-build-apps
    name: Before you begin
    identifier: cloud-add-ip
    weight: 20
type: page
isTocNested: true
showAsideToc: true
---

In addition to having a [cluster deployed in YugabyteDB Managed](../../qs-add/), you need the following to run the sample applications with YugabyteDB Managed:

- **The cluster CA certificate**. YugabyteDB Managed uses TLS 1.2 for communicating with clusters, and digital certificates to verify the identity of clusters. The cluster CA certificate is used to verify the identity of the cluster when you connect to it from an application or client.
- **Your computer added to the cluster IP allow list**. Access to YugabyteDB Managed clusters is limited to IP addresses that you explicitly allow using IP allow lists. To enable applications to connect to your cluster, you need to add your computer's IP address to the cluster IP allow list.

## Download your cluster certificate

To download the certificate to the computer that will be connecting to the cluster, do the following:

1. In YugabyteDB Managed, select your cluster and click **Connect**.
1. Click **YugabyteDB Client Shell** or **Connect to your Application**.
1. Click **Download CA Cert** to download the cluster `root.crt` certificate to your computer.

## Add your computer to the cluster IP allow list

To add your computer to the cluster IP allow list:

1. In YugabyteDB Managed, select your cluster.
1. Click **More Links** and choose **Edit IP Allow List**.
1. Click **Create New List and Add to Cluster**.
1. Click **Detect and add my IP to this list** to add your own IP address.
1. Click **Save**.

The allow list takes up to 30 seconds to become active.

## Learn more

[IP allow lists](../../../cloud-secure-clusters/add-connections/)

[Encryption in transit on YugabyteDB Managed](../../../cloud-secure-clusters/cloud-authentication/)
