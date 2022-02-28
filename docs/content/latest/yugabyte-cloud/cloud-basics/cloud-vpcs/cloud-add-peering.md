---
title: Manage peering connections
headerTitle: 
linkTitle: Peering Connections
description: Manage peering connections to your cloud VPCs.
menu:
  latest:
    identifier: cloud-add-peering
    parent: cloud-vpcs
    weight: 40
isTocNested: true
showAsideToc: true
---

A peering connection connects a Yugabyte Cloud VPC with a VPC on the corresponding cloud provider - typically one that hosts an application that you want to have access to your cluster.

Configuring a peering connection is done in two stages:

1. Create the peering connection in Yugabyte Cloud.
2. Configure the peering in your cloud provider.
    - In AWS, this requires accepting the peering request.
    - In GCP, this requires creating a peering connection.

**Peering Connections** on the **VPC Network** tab displays a list of peering connections configured for your cloud that includes the peering connection name, cloud provider, the network name (GCP) or VPC ID (AWS) of the peered VPC, the name of the Yugabyte VPC, and status of the connection (Pending or Active).

![Peering connections](/images/yb-cloud/cloud-vpc-peering.png)

To view the peering connection details, select a peering connection in the list to display the **Peering Details** sheet.

To terminate a peering connection, click the **Delete** icon for the peering connection in the list you want to terminate, then click **Terminate**. You can also terminate a peering connection by clicking **Terminate Peering** in the **Peering Details** sheet.

{{< note title="Note" >}}

If you have an _Active_ peering connection but are unable to connect to a cluster in the VPC, ensure that you have added the CIDR block of the peered application VPC to your cluster's IP allow list. For information on adding IP allow lists, refer to [Assign IP allow lists](../../add-connections/).

{{< /note >}}

## Create a peering connection

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#peer-aws" class="nav-link active" id="aws-tab" data-toggle="tab" role="tab" aria-controls="peer-aws" aria-selected="true">
      <i class="fab fa-aws" aria-hidden="true"></i>
      AWS
    </a>
  </li>
  <li>
    <a href="#peer-gcp" class="nav-link" id="gcp-tab" data-toggle="tab" role="tab" aria-controls="peer-gcp" aria-selected="false">
      <i class="fab fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="peer-aws" class="tab-pane fade show active" role="tabpanel" aria-labelledby="aws-tab">
    {{% includeMarkdown "peer/peer-aws.md" /%}}
  </div>
  <div id="peer-gcp" class="tab-pane fade" role="tabpanel" aria-labelledby="gcp-tab">
    {{% includeMarkdown "peer/peer-gcp.md" /%}}
  </div>
</div>

## Add the peered application VPC to your cluster IP allow list

Once the VPC and the peering connection are active, you need to add at least one of the CIDR blocks associated with the peered application VPC to the [IP allow list](../../add-connections/) for your cluster.

1. On the **Clusters** page, select your cluster.
1. Click **Quick Links** and **Edit IP Allow List**.
1. Click **Create New List and Add to Cluster**.
1. Enter a name for the allow list.
1. Enter the IP addresses or CIDR.
1. Click **Save**.
