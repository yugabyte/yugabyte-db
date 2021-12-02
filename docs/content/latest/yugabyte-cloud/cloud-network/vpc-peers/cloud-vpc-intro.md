---
title: VPC network overview
headerTitle: 
linkTitle: Overview
description: Requirements and considerations for setting up a VPC network.
menu:
  latest:
    identifier: cloud-vpc-intro
    parent: vpc-peers
    weight: 10
isTocNested: true
showAsideToc: true
---

A virtual private cloud (VPC) is a virtual network that you can define within a cloud provider. Once you create a VPC on a cloud provider, you can then connect it with other VPCs on the same provider. This is called peering. A VPC peering connection is a networking connection between two VPCs on the same cloud provider that enables you to route traffic between them privately, without traversing the public internet. VPC networks provide more secure connections between resources because the network is inaccessible from the public internet and other VPC networks. In addition, traffic in a VPC network doesn’t count against data transfer costs, as the traffic is all effectively local.

In the context of Yugabyte Cloud, when a Yugabyte cluster is deployed in a VPC, it can connect to an application running on a peered VPC as though it was located on the same network; all traffic stays within the cloud provider's network. The VPCs can be in different regions.

![Peered VPCs](/images/yb-cloud/cloud-vpc-diagram.png)

## Advantages

Deploying your cluster in a VPC network has the following advantages:

- Lower network latency. Traffic uses only internal addresses, which provides lower latency than connectivity that uses external addresses.
- Better security. Your services are never exposed to the public Internet.
- Lower data transfer costs. By staying in the provider's network, you won't have any Internet data transfer traffic. (Same region and cross region overages may still apply. Refer to [Data transfer costs](../../../cloud-admin/cloud-billing-costs/#data-transfer-costs).)

## Pricing

There's no additional charge for using a VPC. In most cases, using a VPC will reduce your data transfer costs.

## Locating your VPC

To avoid cross-region data transfer costs, deploy your VPC and cluster in the same region as the application VPC you are peering with.

## Setting the CIDR and sizing your VPC

A VPC is defined by a block of IP addresses, entered in [CIDR notation](https://en.wikipedia.org/wiki/Classless_Inter-Domain_Routing). Because you cannot resize a VPC once it is created, you need to decide on an appropriate size before creating it. Calculate how many applications will be connecting to it, and estimate how that is expected to grow over time. You may want to create a large network to cover all contingencies, but a network that is over-sized can impact network performance. If your traffic experiences spikes, you will need to take that into account.

When entering the range for your VPC in Yugabyte Cloud, the size of the network is determined by the prefix length (the number after the `/`). Yugabyte Cloud supports network sizes from `/24` to `/16` as shown in the following table.

| Network Size (prefix length) | Number of Usable IP Addresses |
| --- | --- | --- |
| /16 | 65536 |
| /17 | 32768 |
| /18 | 16384 |
| /19 | 8192 |
| /20 | 4096 |
| /21 | 2048 |
| /22 | 1024 |
| /23 | 512 |
| /24 | 256 |

You can use the following address ranges (per [RFC 1918](https://datatracker.ietf.org/doc/html/rfc1918)) for your VPCs:

- 10.0.0.0        -   10.255.255.255  (10/8 prefix)
- 172.16.0.0      -   172.31.255.255  (172.16/12 prefix)
- 192.168.0.0     -   192.168.255.255 (192.168/16 prefix)

Addresses cannot overlap with your other VPCs. Yugabyte Cloud warns you when you enter an overlapping range. You can calculate ranges beforehand using [IP Address Guide’s CIDR to IPv4 Conversion calculator](https://www.ipaddressguide.com/cidr).

Yugabyte Cloud reserves the `10.21.0.0.16` range for internal operations.

## Limitations

- Yugabyte Cloud supports AWC and GCP for self-managed VPC peering.
- You assign a VPC when you create a cluster. You cannot change VPCs after cluster creation.
- You cannot change the size of your VPC once it is created.
- VPC network ranges cannot overlap with the ranges of other networks in the same account.
- IP addresses of peered application VPCs must be [added to the IP allow list](../../ip-whitelists/) of your cluster to be able to connect to the database.

## Next steps

- [Set up a VPC network](../cloud-vpc-setup).
