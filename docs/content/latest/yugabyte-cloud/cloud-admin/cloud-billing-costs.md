<!--
title: Cluster costs
linkTitle: Cluster costs
description: Yugabyte Cloud cluster configuration costs.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: cloud-billing-costs
    parent: cloud-admin
    weight: 300
isTocNested: true
showAsideToc: true
-->

Yugabyte offers two billing options: pay-as-you-go (PAYG) billing and subscription billing. The pricing for the two options is provided below:

- Pay-as-you-go (PAYG): USD 0.25/vCPU/hour, minimum 2 vCPUs. Usage is metered in billing increments of a minute, and the Customer pays at the end of the month for actual usage. 

- Subscription: USD 2,200/vCPU for one year, minimum 2 vCPUs. Customer commits to a certain number of vCPUs minimum term of one year, and is invoiced at the start of the contract.

## Cloud Service Provider and Region

Yugabyte Cloud supports deploying clusters onto Amazon Web Services (AWS) and Google Cloud Platform (GCP). The choice of cloud service provider and region or regions for a cluster affects the cost of running the cluster.

Multi-region cluster costs depend on the number of and location of the selected regions. When creating a cluster, the displayed cost is based on only the Preferred Region of the cluster. You can see the total cost of running the cluster in the [Edit Infrastructure](../../cloud-clusters/configure-clusters/#infrastructure) dialog.

## Data transfer

Your cluster includes allowances of 1000GB per month for intra-region transfers, and 10GB per month for all other transfers (cross region and internet). Overages are charged per month as follows:

- Intra region 0.01/GB
- Cross region (APAC) 0.08/GB
- Cross region (other) 0.02/GB
- Internet 0.10/GB

## Disk storage

If you exceed your disk storage allotment, overage charges apply at a rate of $0.10/GB per month.

## Backup storage

100GB/month of basic backup storage is provided for every vCPU in a cluster; more than that and overage charges apply at a rate of $0.025/GB per month.
