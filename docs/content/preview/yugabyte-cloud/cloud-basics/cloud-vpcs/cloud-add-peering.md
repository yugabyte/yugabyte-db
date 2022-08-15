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

A peering connection connects a YugabyteDB Managed VPC with a VPC on the corresponding cloud provider - typically one that hosts an application that you want to have access to your cluster. A VPC must be created before you can configure a peering connection.

> To learn about VPC networking in YugabyteDB Managed, refer to [VPC network overview](../cloud-vpc-intro/).\
> To learn how to create a VPC network, refer to [Create a VPC Network](../cloud-add-vpc-aws/).

**Peering Connections** on the **VPC Network** tab of the **Network Access** page displays a list of peering connections configured for your account that includes the peering connection name, cloud provider, the network name (GCP) or VPC ID (AWS) of the peered VPC, the name of the Yugabyte VPC, and [status](#peering-connection-status) of the connection (Pending or Active).

![Peering connections](/images/yb-cloud/cloud-vpc-peering.png)

To view the peering connection details, select a peering connection in the list to display the **Peering Details** sheet.

To create a peering connection, click **Add Peering Connection**. For details, refer to [Create a VPC Network](../cloud-add-vpc-aws/).

To terminate a peering connection, click the **Delete** icon for the peering connection in the list you want to terminate, then click **Terminate**. You can also terminate a peering connection by clicking **Terminate Peering** in the **Peering Details** sheet.

## Peering connection status

If a peering connection is _Pending_, you need to complete the peering with the corresponding cloud provider. Refer to [Accept the peering request in AWS](../cloud-add-vpc-aws/#accept-the-peering-request-in-aws) or [Complete the peering in GCP](../cloud-add-vpc-gcp/#complete-the-peering-in-gcp).

If you have an _Active_ peering connection but are unable to connect to a cluster in the VPC, ensure that you have added the CIDR block of the peered application VPC to your cluster's IP allow list. For information on adding IP allow lists, refer to [Assign IP allow lists](../../../cloud-secure-clusters/add-connections/).
