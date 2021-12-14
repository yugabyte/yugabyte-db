---
title: VPC network overview
headerTitle: 
linkTitle: Overview
description: Requirements and considerations for setting up a VPC network.
menu:
  latest:
    identifier: cloud-vpc-intro
    parent: cloud-vpcs
    weight: 10
isTocNested: true
showAsideToc: true
---

A virtual private cloud (VPC) is a virtual network that you can define within a cloud provider. Once you create a VPC on a cloud provider, you can then connect it with other VPCs on the same provider. This is called peering. A VPC peering connection is a networking connection between two VPCs on the same cloud provider that enables you to route traffic between them privately, without traversing the public internet. VPC networks provide more secure connections between resources because the network is inaccessible from the public internet and other VPC networks.

In the context of Yugabyte Cloud, when a Yugabyte cluster is deployed in a VPC, it can connect to an application running on a peered VPC as though it was located on the same network; all traffic stays within the cloud provider's network. The VPCs can be in different regions.

![Peered VPCs](/images/yb-cloud/cloud-vpc-diagram.png)

## Advantages

Deploying your cluster in a VPC network has the following advantages:

- Lower network latency. Traffic uses only internal addresses, which provides lower latency than connectivity that uses external addresses.
- Better security. Your services are never exposed to the public Internet.
- Lower data transfer costs. By staying in the provider's network, you won't have any Internet data transfer traffic. (Same region and cross region overages may still apply. Refer to [Data transfer costs](../../../cloud-admin/cloud-billing-costs/#data-transfer-costs).)

## Pricing

There's no additional charge for using a VPC. In most cases, using a VPC will reduce your data transfer costs.

## Choosing the region for your VPC

To avoid cross-region data transfer costs, deploy your VPC and cluster in the same region as the application VPC you are peering with.

### Peering multi-region VPCs

If your cluster spans multiple regions, how you peer with your application VPCs will depend on the cloud provider you use.

GCP
: If you use the default GCP VPC setup, your cluster VPC is global; your peered application VPC automatically has access to all regions.

: - auto vs custom?

AWS
: Create a VPC in each region, and peer your application VPC to each of them - that is, create a separate peering connection for each regional VPC.

## Setting the CIDR and sizing your VPC

A VPC is defined by a block of IP addresses, entered in [CIDR notation](https://en.wikipedia.org/wiki/Classless_Inter-Domain_Routing). Because you cannot resize a VPC once it is created, you need to decide on an appropriate size before creating it. Calculate how many applications will be connecting to it, and estimate how that is expected to grow over time. You may want to create a large network to cover all contingencies, but a network that is over-sized can impact network performance. If your traffic experiences spikes, you will need to take that into account.

When entering the range for your VPC in Yugabyte Cloud, the size of the network is determined by the prefix length (the number after the `/`). Yugabyte Cloud supports network sizes from `/26` to `/16` as shown in the following table.

| Provider | Network Size (prefix length) | Number of Usable IP Addresses | Notes |
| --- | --- | --- | --- | --- |
| GCP (auto)| /16<br>/17<br>/18 | 65536<br>32768<br>16384 | In GCP auto network, the range is split across all supported regions.<br>For information on GCP custom and auto VPCs, refer to [Subnet creation mode](https://cloud.google.com/vpc/docs/vpc#subnet-ranges) in the GCP documentation. |
| AWS and GCP (custom) | /24<br>/25<br>/26 | 256<br>128<br>64 | In AWS or a GCP custom network, the range is assigned to a single region. |

You can use the following address ranges (per [RFC 1918](https://datatracker.ietf.org/doc/html/rfc1918)) for your VPCs:

- 10.0.0.0        -   10.255.255.255  (10/8 prefix)
- 172.16.0.0      -   172.31.255.255  (172.16/12 prefix)
- 192.168.0.0     -   192.168.255.255 (192.168/16 prefix)

Addresses can overlap with other VPCs, but not if they are peered to the same application VPC. Yugabyte Cloud warns you when you enter an overlapping range. You can calculate ranges beforehand using [IP Address Guide’s CIDR to IPv4 Conversion calculator](https://www.ipaddressguide.com/cidr).

Yugabyte Cloud reserves the following ranges for internal operations.

| Provider | Range |
| --- | --- |
| AWS | 10.3.0.0/16<br>10.4.0.0/16 |
| GCP | 10.21.0.0/16 |

## Limitations

- You assign a VPC when you create a cluster. You cannot switch VPCs after cluster creation.
- You cannot change the size of your VPC once it is created.
- You cannot peer VPCs with overlapping ranges with the same application VPC.
- You can create a maximum of 3 AWS VPCs per region.
- You can create a maximum of 3 GCP VPCs.

If you need additional VPCs, contact Yugabyte Support.

## Next steps

- [Set up a VPC network](../cloud-vpc-setup).
