---
title: Cluster costs
linkTitle: Cluster costs
description: YugabyteDB Aeon cluster configuration costs.
headcontent: Plan-based cluster pricing
menu:
  preview_yugabyte-cloud:
    identifier: cloud-billing-costs-1
    parent: cloud-admin
    weight: 400
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../cloud-billing-costs/" class="nav-link active">
      Plan Pricing
    </a>
  </li>

  <li >
    <a href="../cloud-billing-costs-classic/" class="nav-link">
      Classic Pricing
    </a>
  </li>
</ul>

Your bill is calculated based on your usage of the following dimensions:

- Instance vCPU capacity
- Disk storage
- Backup storage
- Data transfer

The following calculations reflect the Standard plan rate card.

## Instance vCPU capacity costs

Instance vCPU capacity cost is the cost for the use of the total number of vCPUs in your account.

{{< tip title="Rate card" >}}

$0.17/hour ($.00283333333/minute)

{{< /tip >}}

You can reduce the cost by reducing the number of vCPUs, which may have a negative impact on performance.

### Calculating instance minutes

Yugabyte measures vCPU use in "Instance-Minutes," which are added up at the end of the month to generate your monthly charges. The total instance capacity cost across all your clusters is the sum of instance-minutes across all clusters multiplied by the base per minute rate card ($.00283333333/minute).

Pricing is per instance minute consumed for each instance, from the time an instance is launched until it is terminated.

Assume you start a cluster with 3 nodes x 2 vCPUs (6 vCPUs) for the first 15 days in September, and then scale up to 6 nodes x 2 vCPUs (12 vCPUs) for the final 15 days in September.

At the end of September, you would calculate the cost as follows.

Total instance-minutes
: [(6 vCPUs x 15 days x 24 hours x 60 min) + (12 vCPUs x 15 days x 24 hours x 60 min)]
: = 388800

Total vCPU cost/month
: Total instance minutes x Per minute base rate

Total vCPU cost/month
: 388800 x $.00283333333 ~ $1101.60

## Disk storage cost

Disk storage costs are tied to the cost of storing the data on disk in the underlying IaaS storage (for example, EBS on AWS).

{{< tip title="Rate card" >}}

$0.10/GB per month ($0.0001388888889/hr for 30-day month)

{{< /tip >}}

You can customize your cluster storage capacity independently of your cluster vCPU capacity.

Disk storage size is calculated by metering the storage space (GB) occupied per cluster. The same unit price applies to all regions and clouds.

### Calculating disk storage cost

Yugabyte measures disk storage in "GB-hours," which are added up at the end of the month to generate your monthly charges. The total disk storage capacity cost across all your clusters is the total number of GB-hours multiplied by the base per hour rate card ($0.0001388888889/hr in a 30-day month).

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

Total disk storage cost/month
: Total disk storage (GB-hours) x Per hour base rate

Total disk storage cost/month
: 648000 x 0.0001388888889 = $90

## Backup storage costs

Backup storage costs are tied to the cost of storing the backup snapshots in the underlying IaaS storage services (that is, S3 on AWS, blob on Azure, or GCS on Google cloud). It's purely a function of total data backed up from your cluster and the retention period.

{{< tip title="Rate card" >}}

Rate card:  $0.025/GB per month ($ 0.00003472222222/hr for 30-day month)

{{< /tip >}}

By default, every cluster is configured with 24 hour backups with an 8 day retention period. You can customize your backup schedule and retention period per cluster. Refer to [Back up clusters](../../cloud-clusters/backup-clusters/).

Backup storage size is calculated by metering the storage space (GB) occupied per cluster. The same unit price applies to all regions and clouds.

### Calculating backup storage cost

Yugabyte measures backup storage in "GB-hours," which are added up at the end of the month to generate your monthly charges. The total backup storage capacity cost across your clusters is the total number of GB-hours multiplied by the base per hour rate card ($ 0.00003472222222/hr in a 30-day month).

Assume you start a cluster with 3 nodes x 2 vCPUs (6 vCPUs) for the first 15 days in September, and then scale up to 6 nodes x 2 vCPUs (12 vCPUs) for the final 15 days in September. Assume also an actual backup usage of 720000 GB-hours.

At the end of September, you would have the following total backup cost.

**Total backup storage cost/month** = Total backup usage (GB-hours) x Per hour base rate

**Total backup storage cost/month** = 720000 x 0.00003472222222 = $25

## Data transfer costs

Data transfer accounts for the volume of data going into, out of, and between the nodes in a cluster, which is summed up to a cumulative amount in a billing cycle.

Yugabyte meters and bills data transfer using the following three dimensions.

### Same region

This accounts for all regional traffic of the cluster. This includes all cross availability zone inter-node traffic, which YugabyteDB automatically manages, and egress cost to a client in the same region as the cluster.

Clusters deployed in a single availability zone (AZ) will have much lower usage than clusters with nodes deployed across multiple AZs.

{{< tip title="Rate card" >}}

$.01/GB

{{< /tip >}}

### Cross region

This accounts for all of the traffic coming out of the cluster to a different region. This happens if a client is using [VPC networking](../../cloud-basics/cloud-vpcs/) but is in different region than the cluster deployments. Different rate cards apply for clusters deployed in Asia-Pacific (APAC) vs other regions.

{{< tip title="Rate card" >}}

APAC $0.08/GB

Other regions $0.02/GB

{{< /tip >}}

### Data out (Internet)

This accounts for all of the traffic coming out of the cluster to the internet. This happens when a client is not using VPC networking and connecting to the cluster over the internet.

{{< tip title="Rate card" >}}

$.10/GB

{{< /tip >}}

### Controlling data transfer costs

- Ensure that queries originate from the same cloud region and provider as your database deployment whenever possible.

  - Avoid data out internet costs by using [VPC networking](../../cloud-basics/cloud-vpcs/) and not allowing any client applications to connect over the public internet.

  - Minimize data out cross region costs by making sure your client application and database cluster are deployed in the same cloud and region and connected using VPC networking.

- Ensure that queries do not:

  - Re-read data that already exists on the client.
  - Re-write existing data to your database deployment.

- If possible, configure your client driver to use network compression to communicate with the YugabyteDB cluster. YugabyteDB Aeon always compresses intra-cluster communication.

## Paused cluster costs

Yugabyte suspends [instance vCPU capacity costs](#instance-vcpu-capacity-costs) for paused clusters. Paused clusters are billed for [disk storage](#disk-storage-cost) and [backup storage](#backup-storage-costs) at the standard rates.

For example, suppose you have a cluster with the following configuration:

- Total number of vCPUs: 1 node x 4 vCPU = 4 vCPUs
- Disk storage used: 200 GB
- Backup storage used: 400 GB

While active, the cluster is charged at the following rate:

**Total vCPU cost/hour** = vCPUs x hourly rate

**Active cluster hourly rate** = 4 x $0.25 = $1/hour

When paused, instance vCPU capacity is no longer charged. Disk and backup storage are charged at the standard rate, as follows:

Disk storage (Paused) = disk storage x hourly rate
: 200gb x 0.000138888889 = $0.0277777778/hour

Backup storage (Paused) = backup storage x hourly rate
: 400gb x 0.00003472222222 = $0.0138888889/hour

**Paused cluster hourly rate** = $0.0416666667/hour

For paused clusters, your invoice includes Disk Storage (Paused) and Backup Storage (Paused) items.
