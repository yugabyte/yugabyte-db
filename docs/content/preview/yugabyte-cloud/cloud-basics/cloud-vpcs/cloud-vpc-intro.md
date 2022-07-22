---
title: VPC network overview
headerTitle:
linkTitle: Overview
description: Requirements and considerations for setting up a VPC network.
menu:
  preview_yugabyte-cloud:
    identifier: cloud-vpc-intro
    parent: cloud-vpcs
    weight: 10
type: docs
---

A virtual private cloud (VPC) is a virtual network that you can define in a cloud provider. After you create a VPC on a cloud provider, you can then connect it with other VPCs on the same provider. This is called peering. A VPC peering connection is a networking connection between two VPCs on the same cloud provider that enables you to route traffic between them privately, without traversing the public internet. VPC networks provide more secure connections between resources because the network is inaccessible from the public internet and other VPC networks.

In the context of YugabyteDB Managed, when a Yugabyte cluster is deployed in a VPC, it can connect to an application running on a peered VPC as though it was located on the same network; all traffic stays in the cloud provider's network. The VPCs can be in different regions.

![Peered VPCs](/images/yb-cloud/cloud-vpc-diagram.png)

## Advantages

Deploying your cluster in a VPC network has the following advantages:

- Lower network latency. Traffic uses only internal addresses, which provides lower latency than connectivity that uses external addresses.
- Better security. Your services are never exposed to the public Internet.
- Lower data transfer costs. By staying in the provider's network, you won't have any Internet data transfer traffic. (Same region and cross region overages may still apply. Refer to [Data transfer costs](../../../cloud-admin/cloud-billing-costs/#data-transfer-costs).)

## Pricing

There's no additional charge for using a VPC. In most cases, using a VPC will reduce your data transfer costs. VPCs are not supported for Sandbox clusters.

## Limitations

- You assign a VPC when you create a cluster. You can't switch VPCs after cluster creation.
- You can't change the size of your VPC once it is created.
- You can't peer VPCs with overlapping ranges with the same application VPC.
- You can create a maximum of 3 AWS VPCs per region.
- You can create a maximum of 3 GCP VPCs.
- VPCs are not supported on Sandbox clusters.

If you need additional VPCs, contact {{% support-cloud %}}.

## Prerequisites

Before setting up the VPC network, you'll need the following:

- The CIDR block you want to use for your VPC.

  - Refer to [Set the CIDR and size your VPC](#set-the-cidr-and-size-your-vpc).

- The details of the application VPC you want to peer with.

  - AWS - the AWS account ID, and the VPC ID, region, and CIDR block. To obtain these details, navigate to your AWS [Your VPCs](https://console.aws.amazon.com/vpc/home?#vpcs) page for the region where the VPC is located.

  - GCP - the project ID and the network name, and CIDR block. To obtain these details, navigate to your GCP [VPC networks](https://console.cloud.google.com/networking/networks) page.

### Choose the region for your VPC

To avoid cross-region data transfer costs, deploy your VPC and cluster in the same region as the application VPC you are peering with.

For GCP, you have the choice of selecting all regions automatically, or defining a custom set of regions. If you use automated region selection, the VPC is created globally and assigned to all regions supported by YugabyteDB Managed. If you use custom region selection, you can choose one or more regions, and specify unique CIDR ranges for each; you can also add regions at a later date.

For AWS, you can only define a single region per VPC.

### Set the CIDR and size your VPC

A VPC is defined by a block of [private IP addresses](#private-ip-address-ranges), entered in [CIDR notation](https://en.wikipedia.org/wiki/Classless_Inter-Domain_Routing). Because you can't resize a VPC once it is created, you need to decide on an appropriate size before creating it. Ideally, you want the network to be as small as possible while accommodating potential growth. Calculate how many applications will be connecting to it, and estimate how that is expected to grow over time. Although you may want to create a large network to cover all contingencies, an over-sized network can impact network performance. If your traffic experiences spikes, you'll need to take that into account.

When entering the range for your VPC in YugabyteDB Managed, the size of the network is determined by the prefix length (the number after the `/`). YugabyteDB Managed supports network sizes from `/26` to `/16` as shown in the following table. For typical applications, `/26` is more than sufficient.

| Provider | Network Size (prefix length) | Number of Usable IP Addresses | Notes |
| --- | --- | --- | --- | --- |
| GCP (auto)| /16<br>/17<br>/18 | 65536<br>32768<br>16384 | In a GCP auto network, the range is split across all supported regions.<br>For information on GCP custom and auto VPCs, refer to [Subnet creation mode](https://cloud.google.com/vpc/docs/vpc#subnet-ranges) in the GCP documentation. |
| GCP (custom) | /24<br>/25<br>/26 | 256<br>128<br>64 | In a GCP custom network, you can customize the regions for the VPC. |
| AWS | /26 | 64 | In AWS, you assign the range to a single region. If you need multiple regions, you must create a separate VPC for each. |

### Private IP address ranges

You can use the private IP addresses in the following ranges (per [RFC 1918](https://datatracker.ietf.org/doc/html/rfc1918)) for your VPCs:

- 10.0.0.0        -   10.255.255.255  (10/8 prefix)
- 172.16.0.0      -   172.31.255.255  (172.16/12 prefix)
- 192.168.0.0     -   192.168.255.255 (192.168/16 prefix)

Peered VPCs also use addresses in these ranges. Once peered, you also need to add the addresses of the peered VPCs to your cluster IP allow list. Private IP addresses added to the cluster allow list that are not part of a peered network are ignored, and can't be used to connect to the cluster.

Addresses have the following restrictions:

- Addresses can overlap with other VPCs, but not if they are peered to the same application VPC. YugabyteDB Managed warns you when you enter an overlapping range.
- Addresses can't overlap with the CIDR of the application VPC you intend to peer with.

- YugabyteDB Managed reserves the following ranges for internal operations.

  | Provider | Range |
  | --- | --- |
  | AWS | 10.3.0.0/16<br>10.4.0.0/16 |
  | GCP | 10.21.0.0/16 |

You can calculate ranges beforehand using [IP Address Guide's CIDR to IPv4 Conversion calculator](https://www.ipaddressguide.com/cidr).

<!--
## Create the VPC network

To create a VPC network, you need to complete the following tasks:

1. Create the VPC. [AWS](../cloud-add-vpc-aws/#create-a-vpc) | [GCP](../cloud-add-vpc-gcp/#create-a-vpc)

    - Reserves a range of IP addresses for the network.
    - The status of the VPC is _Active_ when done.

1. Deploy the cluster in the VPC. [AWS](../cloud-add-vpc-aws/#deploy-a-cluster-in-the-vpc) | [GCP](../cloud-add-vpc-gcp/#deploy-a-cluster-in-the-vpc)

    - This can be done at any time - you don't need to wait until the VPC is peered.

1. Create a peering connection. [AWS](../cloud-add-vpc-aws/#create-a-peering-connection) | [GCP](../cloud-add-vpc-gcp/#create-a-peering-connection)

    - Connects your VPC and the application VPC on the cloud provider network.
    - The status of the peering connection is _Pending_ when done.

1. Configure the cloud provider.

    - Confirms the connection between your VPC and the application VPC.
    - Performed in the cloud provider settings.
      - In AWS, [accept the peering request](../cloud-add-vpc-aws/#accept-the-peering-request-in-aws).
      - In GCP, [create a peering connection](../cloud-add-vpc-gcp/#create-a-peering-connection-in-gcp).
    - The status of the peering connection is _Active_ when done.

1. [Add the application VPC to the cluster IP allow list](../../../cloud-secure-clusters/add-connections/).

    - Allows the peered application VPC to connect to the cluster.
    - Add at least one of the CIDR blocks associated with the peered application VPC to the IP allow list for your cluster.

With the exception of 4, these tasks are performed in YugabyteDB Managed.
-->

## Next step

[Create a VPC network](../cloud-add-vpc-aws/)
