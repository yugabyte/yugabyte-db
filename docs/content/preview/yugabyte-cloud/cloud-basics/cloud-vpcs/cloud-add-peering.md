---
title: Peering connections
headerTitle:
linkTitle: Peering connections
description: Manage peering connections to your cloud VPCs.
headcontent: Manage peering connections to your VPCs
menu:
  preview_yugabyte-cloud:
    identifier: cloud-add-peering
    parent: cloud-vpcs
    weight: 30
type: docs
---

A peering connection connects a YugabyteDB Aeon VPC with a VPC on the same cloud provider (AWS or GCP) - typically one that hosts an application that you want to have access to your cluster. Peering connections are not supported in Azure (use a [private service endpoint](../cloud-add-endpoint/)).

{{< youtube id="Yu59wX8Rsug" title="Create a peering connection to access a cluster in YugabyteDB Aeon" >}}

In the context of YugabyteDB Aeon, when a Yugabyte cluster is deployed in a VPC, it can connect to an application running on a peered VPC as though it was located on the same network; all traffic stays in the cloud provider's network. The VPCs can be in different regions, but ideally should be in the same region. You must create a VPC before you can configure a peering connection.

- To learn how to peer VPCs, refer to [Peer VPCs](../cloud-add-vpc-aws/).

![VPC network using peering](/images/yb-cloud/managed-vpc-diagram.png)

**Peering Connections** on the **VPC Network** tab of the **Networking** page displays a list of peering connections configured for your account that includes the peering connection name, cloud provider, the network name (GCP) or ID (AWS) of the peered VPC, the name of the YugabyteDB VPC, and [status](#peering-connection-status) of the connection (Pending or Active).

![Peering connections](/images/yb-cloud/cloud-vpc-peering.png)

To view the peering connection details, select a peering connection in the list to display the **Peering Details** sheet.

To create a peering connection, click **Add Peering Connection**. For details, refer to [Peer VPCs](../cloud-add-vpc-aws/).

To terminate a peering connection, click the **Delete** icon for the peering connection in the list you want to terminate, then click **Terminate**. You can also terminate a peering connection by clicking **Terminate Peering** in the **Peering Details** sheet.

## Peering connection status

If a peering connection is _Pending_, you need to complete the peering with the corresponding cloud provider. Refer to [Accept the peering request in AWS](../cloud-add-vpc-aws/#accept-the-peering-request-in-aws) or [Complete the peering in GCP](../cloud-add-vpc-gcp/#complete-the-peering-in-gcp).

If you have an _Active_ peering connection but are unable to connect to a cluster in the VPC, ensure that you have added the CIDR block of the peered application VPC to your cluster's IP allow list. For information on adding IP allow lists, refer to [Assign IP allow lists](../../../cloud-secure-clusters/add-connections/).
