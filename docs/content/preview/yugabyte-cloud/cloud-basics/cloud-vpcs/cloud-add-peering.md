---
title: Peering connections
headerTitle:
linkTitle: Peering connections
description: Manage peering connections to your cloud VPCs.
menu:
  preview:
    identifier: cloud-add-peering
    parent: cloud-vpcs
    weight: 40
isTocNested: true
showAsideToc: true
---

A peering connection connects a YugabyteDB Managed VPC with a VPC on the corresponding cloud provider - typically one that hosts an application that you want to have access to your cluster.

**Peering Connections** on the **VPC Network** tab displays a list of peering connections configured for your account that includes the peering connection name, cloud provider, the network name (GCP) or VPC ID (AWS) of the peered VPC, the name of the Yugabyte VPC, and status of the connection (Pending or Active).

![Peering connections](/images/yb-cloud/cloud-vpc-peering.png)

To view the peering connection details, select a peering connection in the list to display the **Peering Details** sheet.

To terminate a peering connection, click the **Delete** icon for the peering connection in the list you want to terminate, then click **Terminate**. You can also terminate a peering connection by clicking **Terminate Peering** in the **Peering Details** sheet.

{{< warning title="Important" >}}

If you have an _Active_ peering connection but are unable to connect to a cluster in the VPC, ensure that you have added the CIDR block of the peered application VPC to your cluster's IP allow list. For information on adding IP allow lists, refer to [Assign IP allow lists](../../../cloud-secure-clusters/add-connections/).

{{< /warning >}}
