---
title: Cluster costs
linkTitle: Cluster costs
description: YugabyteDB Managed cluster configuration costs.
headcontent: YugabyteDB Managed cluster configuration costs
image: /images/section_icons/deploy/enterprise.png
menu:
  preview_yugabyte-cloud:
    identifier: cloud-billing-costs
    parent: cloud-admin
    weight: 400
type: docs
---

There are no set-up charges or commitments to begin using YugabyteDB Managed. At the end of the month, you are automatically charged for that month's usage. You can view your charges for the current billing period at any time by selecting the **Invoices** tab on the **Billing** page. Refer to [Manage your billing profile and payment method](../cloud-billing-profile/).

Your bill is calculated based on your usage of the following dimensions:

- Instance vCPU capacity
- Disk storage
- Backup storage
- Data transfer

Instance vCPU capacity makes up the majority of your bill, and is the easiest to understand and control. It's purely a function of your total number of vCPUs used and how long they have been running. The cluster's per-hour charge includes free allowances for disk storage, backup storage, and data transfer. If you use more than the free allowance, you incur overages on top of the base vCPU capacity cost.

| Dimension | Allowance/vCPU per month |
|---|---|
| Disk storage | 50 GB |
| Backup storage | 100 GB |
| Data transfer – Same Region | 1000 GB |
| Data transfer – Cross Region (APAC) | 10 GB |
| Data transfer – Cross Region (Other regions) | 10 GB |
| Data transfer – Internet | 10 GB |

You can see the approximate cost for your vCPUs when [creating](../../cloud-basics/create-clusters/) and [scaling](../../cloud-clusters/configure-clusters/) clusters, as shown in the following illustration. **+ Usage** in this context refers to any potential overages from exceeding the free allowances for disk storage, backup storage, and data transfer.

![Cluster Settings Edit Infrastructure](/images/yb-cloud/cloud-clusters-settings-edit.png)

## Instance vCPU capacity costs

Instance vCPU capacity cost is the cost for the use of the total number of vCPUs in your account.

{{< tip title="Rate card" >}}

$0.25/hour ($.00416666666/minute)

{{< /tip >}}

You can reduce the cost by reducing the number of vCPUs, which may have a negative impact on performance.

### Calculating instance minutes

Yugabyte measures vCPU use in "Instance-Minutes," which are added up at the end of the month to generate your monthly charges. The total instance capacity cost across all your clusters is the sum of instance-minutes across all clusters multiplied by the base per minute rate card ($.00416666666/minute).

Pricing is per instance minute consumed for each instance, from the time an instance is launched until it is terminated.

Assume you start a cluster with 3 nodes x 2 vCPUs (6 vCPUs) for the first 15 days in September, and then scale up to 6 nodes x 2 vCPUs (12 vCPUs) for the final 15 days in September.

At the end of September, you would calculate the cost as follows.

Total instance-minutes
: [(6 vCPUs x 15 days x 24 hours x 60 min) + (12 vCPUs x 15 days x 24 hours x 60 min)]
: = 388800

Total vCPU cost/month
: Total instance minutes x Per minute base rate

Total vCPU cost/month
: 388800 x $.00416666666 ~ $1619.99

## Disk storage cost

Disk storage costs are tied to the cost of storing the data on disk in the underlying IaaS storage (for example, EBS on AWS).

{{< tip title="Rate card" >}}

$0.10/GB per month ($0.0001388888889/hr for 30-day month)

{{< /tip >}}

The free allowance for disk storage is 50 GB/month for every 1 vCPU per month used in a cluster. Whenever you exceed the 50 GB/month/vCPU threshold, you are billed for the storage used in excess of the free allowance.

You can also specify a custom value greater than free allowance storage capacity. You can customize your cluster storage capacity independently of your cluster vCPU capacity. If you customize an amount of disk storage greater than the free allowance, you are only charged for the amount exceeding the free allowance.

For example, a 3 node x 2 vCPU (6 vCPUs) cluster includes a total free allowance of 300 GB/month (6 vCPUs x 50 GB), which is equally distributed across 3 nodes at 100 GB each. For the same cluster, if you increase per node storage capacity to 150 GB, then your total disk storage will be 450 GB (3 nodes x 150 GB) but you are only charged for the 150 GB above your 300 GB allowance.

Disk storage size is calculated by metering the storage space (GBs) occupied per cluster. The same unit price applies to all regions and clouds.

### Calculating disk storage cost

Yugabyte measures disk storage in "GB-hours," which are added up at the end of the month to generate your monthly charges. The total disk storage capacity cost across all your clusters is the total number of GB-hours multiplied by the base per hour rate card ($0.0001388888889/hr in a 30-day month), less the total free allowance based on per month vCPU usage.

Assume you start a cluster for the first 15 days of September with the following configuration:

- Total number of vCPUs: 3 nodes x 2 vCPU = 6 vCPUs
- Disk storage/node: 100 GB
- Total disk storage: 300 GB

Then scale up to the following configuration for the final 15 days in September:

- Total number of vCPUs: 3 nodes x 4 vCPU = 12 vCPUs
- Disk storage/node: 500 GB
- Total disk storage: 1500 GB

At the end of September, you would have the following total usage cost:

Total disk storage
: [(300 GB x 15 days x 24 hours) + (1500 GB x 15 days x 24 hours)]
: = 648000 GB-hours

Total instance-minutes
: [(6 vCPUs x 15 days x 24 hours x 60 min) + (12 vCPUs x 15 days x 24 hours x 60 min)]
: = 388800 instance-minutes

Total vCPUs
: 388800 instance-minutes / ( 30 days x 24 hours x 60 minutes )
: = 9 vCPUs

Free allowance (GB/month)
: 9 vCPUs x 50 GB/month = 450 GB

Free allowance (GB-hours)
: 450 GB x 30 days x 24 hours = 324000 GB-hours

Disk storage overages
: 648000 GB-hours - 324000 GB-hours = 324000 GB-hours

Total disk storage cost/month
: Total overages (GB-hours) x Per hour base rate

Total disk storage cost/month
: 324000 x 0.0001388888889 = $45

## Backup storage costs

Backup storage costs are tied to the cost of storing the backup snapshots in the underlying IaaS storage services (that is, S3 on AWS, blob on Azure, or GCS on Google cloud). It's purely a function of total data backed up from your cluster and the retention period.

{{< tip title="Rate card" >}}

Rate card:  $0.025/GB per month ($ 0.00003472222222/hr for 30-day month)

{{< /tip >}}

The free allowance for backup storage is 100 GB/month for every 1 vCPU per month used in a cluster. Whenever you exceed the 100 GB/month/vCPU threshold, you are billed for the backup storage used in excess of the free allowance. For example, a 3 node x 2 vCPU (6 vCPUs) cluster includes a total free allowance of 600 GB/month (6 vCPUs x 100 GB).

By default, every cluster is configured with 24 hour backups with an 8 day retention period. You can customize your backup schedule and retention period per cluster. Taking frequent backups and retaining for a long period of time can lead to overages. Refer to [Back up clusters](../../cloud-clusters/backup-clusters/).

Backup storage size is calculated by metering the storage space (GBs) occupied per cluster. The same unit price applies to all regions and clouds.

### Calculating backup storage cost

Yugabyte measures backup storage in "GB-hours," which are added up at the end of the month to generate your monthly charges. The total backup storage capacity cost across your clusters is the total number of GB-hours multiplied by the base per hour rate card ($ 0.00003472222222/hr in a 30-day month), less the total free allowance based on per month vCPU usage.

Assume you start a cluster with 3 nodes x 2 vCPUs (6 vCPUs) for the first 15 days in September, and then scale up to 6 nodes x 2 vCPUs (12 vCPUs) for the final 15 days in September. Assume also an actual backup usage of 720000 GB-hours.

At the end of September, you would have the following total backup cost.

Total instance-minutes
: [(6 vCPUs x 15 days x 24 hours x 60 min) + (12 vCPUs x 15 days x 24 hours x 60 min)]
: = 388800 instance-minutes

Total vCPUs
: 388800 instance-minutes / ( 30 days x 24 hours x 60 minutes )
: = 9 vCPUs

Free allowance (GB-month)
: 9 vCPUs x 100 GB/month = 900 GB

Free allowance (GB-hours)
: 900 GB x 30 days x 24 hours = 648000 GB-hours

Backup storage overages
: 720000 GB-hours - 648000 GB-hours = 72000 GB-hours

**Total backup storage cost/month** = Total overages (GB-hours) x Per hour base rate

**Total backup storage cost/month** = 72000 x 0.00003472222222 = $2.50

## Data transfer costs

Data Transfer accounts for the volume of data going into, out of, and between the nodes in a cluster, which is summed up to a cumulative amount in a billing cycle.

Yugabyte meters and bills data transfer using the following three dimensions.

### Same region

This accounts for all regional traffic of the cluster. This includes all cross availability zone inter-node traffic, which YugabyteDB automatically manages, and egress cost to a client in the same region as the cluster.

Single-node ([fault tolerance](../../cloud-basics/create-clusters/#cluster-settings) of NONE) and three-node (fault tolerance of Node Level) with single availability zone (AZ) topologies will have much lower usage than clusters with three nodes (fault tolerance of Availability Zone) deployed across multiple AZs.

{{< tip title="Rate card" >}}

$.01/GB

{{< /tip >}}

The free allowance for same region transfers is 1000 GB per month for every 1 vCPU per month used in a cluster. You are charged for any data transfer used in excess of the free allowance.

### Cross region

This accounts for all of the traffic coming out of the cluster to a different region. This happens if a client is using [VPC networking](../../cloud-basics/cloud-vpcs/) but is in different region than the cluster deployments. Different rate cards apply for clusters deployed in Asia-Pacific (APAC) vs other regions.

{{< tip title="Rate card" >}}

APAC $0.08/GB

Other regions $0.02/GB

{{< /tip >}}

The free allowance for cross region transfers is 10 GB per month for every 1 vCPU per month used in a cluster. You are charged for any data transfer used in excess of the free allowance.

### Data out (Internet)

This accounts for all of the traffic coming out of the cluster to the internet. This happens when a client is not using VPC networking and connecting to the cluster over the internet.

{{< tip title="Rate card" >}}

$.10/GB

{{< /tip >}}

The free allowance for data out transfers is 10GB per month for every 1 vCPU per month used in a cluster. You are charged for any data transfer used in excess of the free allowance.

### Controlling data transfer costs

- Ensure that queries originate from the same cloud region and provider as your database deployment whenever possible.

  - Avoid data out internet costs by using [VPC networking](../../cloud-basics/cloud-vpcs/) and not allowing any client applications to connect over the public internet.

  - Minimize data out cross region costs by making sure your client application and database cluster are deployed in the same cloud and region and connected using VPC networking.

- Ensure that queries do not:

  - Re-read data that already exists on the client.
  - Re-write existing data to your database deployment.

- If possible, configure your client driver to use wire protocol compression to communicate with the YugabyteDB cluster. YugabyteDB Managed always compresses intra-cluster communication.

## Paused cluster costs

Yugabyte suspends [instance vCPU capacity costs](#instance-vcpu-capacity-costs) for paused clusters. Paused clusters are billed for [disk storage](#disk-storage-cost) and [backup storage](#backup-storage-costs) at the standard rates; this cost includes any storage that is normally covered by your running cluster free allowances.

For example, suppose you have a cluster with the following configuration:

- Total number of vCPUs: 1 node x 4 vCPU = 4 vCPUs
- Disk storage used: 200 GB
- Backup storage used: 400 GB

While active, disk and backup storage are covered by the free allowance, and the cluster is charged at the following rate:

**Total vCPU cost/hour** = vCPUs x hourly rate

**Active cluster hourly rate** = 4 x $0.25 = $1/hour

When paused, instance vCPU capacity is no longer charged, while disk and backup storage are charged at the standard rate, as follows:

Disk storage (Paused) = disk storage x hourly rate
: 200gb x 0.000138888889 = $0.0277777778/hour

Backup storage (Paused) = backup storage x hourly rate
: 400gb x 0.00003472222222 = $0.0138888889/hour

**Paused cluster hourly rate** = $0.0416666667/hour

For paused clusters, your invoice includes Disk Storage (Paused) and Backup Storage (Paused) items.

Yugabyte recalculates the monthly entitlements for disk storage, backup storage, and data transfer after resuming the cluster.
