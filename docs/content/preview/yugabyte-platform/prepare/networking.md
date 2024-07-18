---
title: YugabyteDB Anywhere networking requirements
headerTitle: Networking requirements
linkTitle: Networking
description: Prepare your networking and ports for deploying YugabyteDB Anywhere.
headContent: Port settings for YugabyteDB Anywhere
menu:
  preview_yugabyte-platform:
    identifier: networking
    parent: prepare
    weight: 10
type: docs
---

YugabyteDB Anywhere (YBA) needs to be able to access nodes that will be used to create universes, and the nodes that make up universes need to be accessible to each other and to applications.

## Global port requirements

![YugabyteDB Anywhere network and ports](/images/yb-platform/prepare/yba-networking.png)

The following ports need to be open.

| From | To | Requirements |
| :--- | :--- | :--- |
| DB&nbsp;nodes | DB&nbsp;nodes | Open the following ports for communication between nodes in clusters. They do not need to be exposed to your application. For universes with [Node-to-Node encryption in transit](../../security/enable-encryption-in-transit/), communication over these ports is encrypted.<ul><li>7000 - YB-Master HTTP(S)</li><li>7100 - YB-Master RPC</li><li>9000 - YB-TServer HTTP(S)</li><li>9100 - YB-TServer RPC</li><li>18018 - YB Controller RPC</li></ul> |
| YBA&nbsp;node | DB nodes | Open the following ports on database cluster nodes so that YugabyteDB Anywhere can provision them.<ul><li>22 - SSH</li><li>5433 - YSQL server</li><li>7000/7100 - YB-Master HTTP/RPC</li><li>9000/9100 - YB-TServer HTTP/RPC</li><li>9042 - YCQL server</li><li>9070 - Node agent RPC</li><li>9300 - Prometheus Node Exporter HTTP</li><li>12000 - YCQL API</li><li>13000 - YSQL API</li><li>18018 - YB Controller RPC</li></ul>SSH is not required after initial setup and configuration, but is recommended for subsequent troubleshooting. If you disallow SSH entirely, you must manually set up each DB node (see [Provisioning on-premises nodes](../server-nodes-software/software-on-prem-manual/)). |
| Application | DB nodes | Open the following ports on database cluster nodes so that applications can connect via APIs. For universes with [Client-to-Node encryption in transit](../../security/enable-encryption-in-transit/), communication over these ports is encrypted. Universes can also be configured with database [authorization](../../security/authorization-platform/) and [authentication](../../security/authentication/) to manage access.<ul><li>5433 - YSQL server</li><li>9042 - YCQL server</li></ul> |
| DB nodes | YBA&nbsp;node | Open the following port on the YugabyteDB Anywhere node so that node agents can communicate.<ul><li>443 - HTTPS</li></ul> |
| Operator | YBA&nbsp;node | Open the following ports on the YugabyteDB Anywhere node so that administrators can access the YBA UI and monitor the system and node metrics. These ports are also used by standby YBA instances in [high availability](../../administer-yugabyte-platform/high-availability/) setups.<ul><li>443 - HTTPS</li><li>9090 - Served by Prometheus, for metrics</li></ul>Port 5432 serves a local PostgreSQL instance, and is not exposed outside of localhost. |
| DB nodes<br>YBA node | Storage | Database clusters must be allowed to contact backup storage (such as AWS S3, GCP GCS, Azure blob).<ul><li>443 - HTTPS</li></ul> |

### Networking for xCluster

When two database clusters are connected via [xCluster replication](../../create-deployments/async-replication-platform/), you need to ensure that the yb-master and yb-tserver RPC ports (default 7100 and 9100 respectively) are open in both directions between all nodes in both clusters.

### Overriding default port assignments

When [deploying a universe](../../create-deployments/create-universe-multi-zone/), you can customize the following ports:

- YB-Master HTTP(S)
- YB-Master RPC
- YB-TServer HTTP(S)
- YB-TServer RPC
- YSQL server
- YSQL API
- YCQL server
- YCQL API
- Prometheus Node Exporter HTTP

If you intend to customize these port numbers, replace the default port assignments with the values identifying the port that each process should use. Any value from `1024` to `65535` is valid, as long as this value does not conflict with anything else running on nodes to be provisioned.

## Provider-specific requirements

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#onprem" class="nav-link active" id="onprem-tab" data-bs-toggle="tab"
      role="tab" aria-controls="onprem" aria-selected="true">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>
  <li>
    <a href="#aws" class="nav-link" id="aws-tab" data-bs-toggle="tab"
      role="tab" aria-controls="aws" aria-selected="false">
      <i class="fa-brands fa-aws"></i>
      AWS
    </a>
  </li>
  <li>
    <a href="#gcp" class="nav-link" id="gcp-tab" data-bs-toggle="tab"
      role="tab" aria-controls="gcp" aria-selected="false">
      <i class="fa-brands fa-google"></i>
      GCP
    </a>
  </li>
  <li>
    <a href="#azure" class="nav-link" id="azure-tab" data-bs-toggle="tab"
      role="tab" aria-controls="azure" aria-selected="false">
      <i class="fa-brands fa-microsoft"></i>
      Azure
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="onprem" class="tab-pane fade show active" role="tabpanel" aria-labelledby="onprem-tab">

In on-premises environments, both network and local firewalls on the VM (such as iptables or ufw) need to be configured for the required networking.

&nbsp;

&nbsp;

&nbsp;

&nbsp;

&nbsp;

&nbsp;

  </div>

  <div id="aws" class="tab-pane fade" role="tabpanel" aria-labelledby="aws-tab">

In AWS, you need to identify the specific [Virtual Private Cloud](https://docs.aws.amazon.com/vpc/latest/userguide/what-is-amazon-vpc.html) (VPCs) and subnets in the regions in which YBA and database VMs will be deployed, and the security groups you intend to use for these VMs.

When YBA and database nodes are all deployed in the same VPC, achieving the required network configuration primarily involves setting up the [appropriate security group](https://docs.aws.amazon.com/vpc/latest/userguide/vpc-security-groups.html)(s).

However, if YBA and database nodes are in different VPCs (for example, in a multi-region cluster), the setup is more complex. Use [VPC peering](https://docs.aws.amazon.com/vpc/latest/peering/vpc-peering-basics.html) to open the required ports, in addition to setting up the appropriate security groups.

In the VPC peering approach, peering connections must be established in an N x N matrix, such that every VPC in every region you configure is peered to every other VPC in every other region. [Routing table entries](https://docs.aws.amazon.com/vpc/latest/peering/vpc-peering-routing.html) in every regional VPC should route traffic to every other VPC CIDR block across the PeeringConnection to that respective VPC. Security groups in each VPC can be hardened by only opening up the relevant ports to the CIDR blocks of the VPCs from which you are expecting traffic.

If you deploy YBA in a different VPC than the ones in which you intend to deploy database nodes, then the YBA VPC must also be part of this VPC mesh, and you need to set up routing table entries in the source VPC (YBA) and allow one further CIDR block (or public IP) ingress rule on the security groups for the YugabyteDB nodes (to allow traffic from YBA or its VPC).

When creating the AWS provider, YBA needs to know whether the database nodes will have Internet access, or be airgapped. If your nodes will have Internet access, you must do one of the following:

- Assign public IP addresses to the VMs.
- Set up a NAT gateway. You must configure the NAT gateway before creating the VPC. For more information, see [NAT](https://docs.aws.amazon.com/vpc/latest/userguide/vpc-nat.html) and [Creating a VPC with public and private subnets](https://docs.aws.amazon.com/AmazonECS/latest/developerguide/create-public-private-vpc.html) in the AWS documentation.

  </div>

  <div id="gcp" class="tab-pane fade" role="tabpanel" aria-labelledby="gcp-tab">

In GCP, YBA deploys all database cluster VMs in a single [GCP VPC](https://cloud.google.com/vpc/docs/vpc). You need to identify the VPC and specific subnets for the regions in which VMs are deployed and configure the appropriate [GCP firewall rules](https://cloud.google.com/firewall/docs/using-firewalls) to open the required ports.

Configure firewall rules to allow YBA to communicate with database nodes, and database nodes to communicate with each other, as per the [network diagram](#global-port-requirements).

If the YBA VM and the database cluster VMs are deployed in separate GCP VPCs, you must also peer these VPCs to enable the required connectivity. Typically, routes for the peered VPCs are [set up automatically](https://cloud.google.com/vpc/docs/vpc-peering#route-exchange-options) but may need to be configured explicitly in some cases.

When creating the GCP provider, YBA needs to know whether the database nodes will have Internet access, or be airgapped. If your nodes will have Internet access, you must do one of the following:

- Assign public IP addresses to the VMs.
- Set up a NAT gateway. You must configure the NAT gateway before creating the VPC. For more information, see [network address translation (NAT) gateway](https://cloud.google.com/nat/docs/overview) in the GCP documentation.

  </div>

  <div id="azure" class="tab-pane fade" role="tabpanel" aria-labelledby="azure-tab">

In Azure, you will need to identify the specific [Virtual Networks](https://learn.microsoft.com/en-us/azure/virtual-network/virtual-networks-overview) and subnets for the regions in which the YBA and database VMs are deployed and the (optional) [network security groups](https://learn.microsoft.com/en-us/azure/virtual-network/network-security-groups-overview) (NSG) intended to be associated with these VMs.

When YBA and database cluster nodes are all deployed in the same VPC, configuring the network requirements involves setting up the appropriate network security groups (NSGs) to allow the network configuration. NSGs can either be set up on the subnets directly or specified to YBA for associating with the database cluster VMs.

When YBA and database nodes are deployed in different VPCs (for example, in a multi-region universe), the setup is more complex. Use [VPC peering](https://learn.microsoft.com/en-us/azure/virtual-network/tutorial-connect-virtual-networks-porta), as well as setting up the appropriate NSGs. In the VPC peering approach, peering connections must be established in an N x N matrix, such that every VPC in every region you configure must be peered to every other VPC in every other region. [Routes for the peering connections](https://learn.microsoft.com/en-us/azure/virtual-network/virtual-networks-udr-overview#:~:text=Virtual%20network%20(VNet)%20peering) can be automatically configured by Azure or configured manually. If you deploy YBA in a different VPC than where you intend to deploy database VMs, then this VPC must also be part of the VPC mesh.

  </div>
</div>
