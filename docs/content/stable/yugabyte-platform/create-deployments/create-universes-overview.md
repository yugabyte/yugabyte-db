---
title: Plan your universe
linkTitle: Plan your universe
description: Plan a universe in YugabyteDB Anywhere.
headcontent: Before deploying a production universe, consider the following factors
menu:
  stable_yugabyte-platform:
    identifier: create-universes-overview
    parent: create-deployments
    weight: 5
type: docs
---

{{< page-finder/head text="Plan your deployment" subtle="across different products">}}
  {{< page-finder/list icon="/icons/database-hover.svg" text="YugabyteDB" url="../../deploy/" >}}
  {{< page-finder/list icon="/icons/server-hover.svg" text="YugabyteDB Anywhere" current="" >}}
  {{< page-finder/list icon="/icons/cloud-hover.svg" text="YugabyteDB Aeon" url="/stable/yugabyte-cloud/cloud-basics/create-clusters-overview/" >}}
{{< /page-finder/head >}}

## Summary of best practices

The following best practices are recommended for production universes.

| Feature | Recommendation |
| :--- | :--- |
| [Provider and region](#provider-and-region) | Deploy using a [provider configuration](../../configure-yugabyte-platform/) in the same cloud and regions as your application. YugabyteDB Anywhere supports AWS, Azure, GCP, on-premises, and Kubernetes. |
| [Placement](#placement) | Region or Availability zone resilience, with a minimum of three nodes across multiple regions or AZs.<br>Use Guided mode for most topologies. |
| [Hardware](#hardware) | For most production applications, at least 3 nodes with 4 to 8 vCPUs per node. |
| [YugabyteDB version](#yugabytedb-version) | Use a stable [LTS release](../../../releases/versioning/#stable-release-support-policy). |
| [Staging universe](#staging-universe) | Use a staging universe to test application compatibility with database updates before upgrading your production universe. |
| [Backups](#backups) | Configure [backup storage](../../back-up-restore-universes/configure-backup-storage/) and a [scheduled backup](../../back-up-restore-universes/schedule-data-backups/) policy before going to production. |
| [Security](#security) | Enable [encryption in transit](../../security/enable-encryption-in-transit/) and [encryption at rest](../../security/enable-encryption-at-rest/). Restrict network access to YugabyteDB RPC ports. Refer to [Networking](../../prepare/networking/). |

## In depth

{{<tags/ui/new>}} Placement in the following sections describes the New UI, which uses Guided and Expert modes. To enable the New UI, refer to [New experience](../../yba-overview/#enable-the-new-experience). For steps to create a universe, refer to [Create universes](../create-universes-wizard/).

### Topology

A YugabyteDB universe typically consists of three or more nodes that communicate with each other and across which data is distributed. You can place the nodes in a single availability zone, across multiple zones in a single region, and across regions. With more advanced topologies, you can attach a read replica cluster, pin data to geographic regions, or replicate asynchronously to another universe. The [topology](../../../explore/multi-region-deployments/) you choose depends on your requirements for latency, availability, and geo-distribution.

#### Single region

Single-region universes are available in the following topologies:

- **Single availability zone**. Resilient to node outages.
- **Multiple availability zones**. Resilient to node and availability zone outages.

Cloud providers design zones to minimize the risk of correlated failures caused by physical infrastructure outages like power, cooling, or networking. In other words, single failure events usually affect only a single zone. By deploying nodes across zones in a region, you get resilience to a zone outage as well as high availability.

Single-region universes are not resilient to region-level outages.

#### Multiple region

Multi-region universes are resilient to region-level outages (when data is replicated across regions), and are available in the following topologies:

- **Replicate across regions**. Universe nodes are deployed across 3 or more regions, with data replicated synchronously. Refer to [Synchronous multi region](../../../explore/multi-region-deployments/synchronous-replication-yba/).
- **Partition by region**. Cluster nodes are deployed in separate regions. Data is pinned to specific geographic regions using [row-level geo-partitioning](../../../explore/multi-region-deployments/row-level-geo-partitioning/). Allows fine-grained control over pinning rows in a user table to specific geographic locations.
- **Read replica**. Replica clusters are added to an existing primary cluster and deployed in separate regions, typically remote from the primary. Data is written in the primary cluster and copied to the read replicas, where it can be read. The primary cluster still gets all write requests, while read requests can go either to the primary cluster or to the read replica clusters depending on which is closest. Refer to [Add a read replica](../read-replicas/).
- **xCluster**. Two universes are deployed in separate regions. Data is replicated asynchronously between them, either in one direction or bidirectionally. Use xCluster for [disaster recovery](../../back-up-restore-universes/disaster-recovery/) or [asynchronous replication](../xcluster-replication/).

You can also deploy a single universe across multiple cloud providers using an [on-premises provider](../create-universe-multi-cloud/).

For more details, refer to [Multi-region deployments](../../../explore/multi-region-deployments/).

### Provider and region

#### Provider

YugabyteDB Anywhere deploys universes using a [provider configuration](../../configure-yugabyte-platform/) that describes your environment — regions, availability zones, images, networking, and (for public clouds) the credentials YBA uses to create nodes.

Your choice of provider depends primarily on where your applications run and how much automation you want YBA to have.

| | On-premises | Cloud<br>(AWS, Azure, GCP) | Kubernetes |
| :--- | :--- | :--- | :--- |
| Advantages | Maximum flexibility | Maximum automation | Native to Kubernetes |
| Platforms | Private cloud, bare metal, or public cloud VMs you manage | AWS, Azure, GCP | Kubernetes (including Tanzu and OpenShift) |
| Node provisioning | You create VMs; YBA takes nodes from a free pool | YBA creates and provisions VMs | Via Helm |
| Permissions for YBA | Minimal sudo access during provisioning | Cloud and OS permissions | As required for Kubernetes |

Not sure which provider to use? Refer to [Provider configurations](../../yba-overview/#provider-configurations).

You must create a provider configuration before you create a universe. For Kubernetes, see [Create Kubernetes provider configuration](../../configure-yugabyte-platform/kubernetes/).

#### Region

For best performance as well as lower data transfer costs, locate your universe as close to your applications as possible:

- Use the same cloud provider as your application.
- Locate universe nodes in the same region as your application.

You can only select regions (and availability zones) that have been added to the provider configuration.

For on-premises providers, ensure the free pool has enough nodes in the regions and zones you plan to use.

#### Instance types

When you create a universe, you choose an instance type from those available in the provider and selected regions. YBA uses the same instance type for all TServer nodes in the cluster.

For public clouds, instance availability varies by region. For on-premises, instance type is informational — you provision the VMs. For Kubernetes, you set cores, memory, and volume size instead of a cloud instance type.

### Placement

YugabyteDB achieves resiliency by replicating data across fault domains using the [Raft consensus protocol](../../../architecture/docdb-replication/replication/). The fault domain can be at the level of individual nodes, availability zones, or entire regions.

Placement determines how resilient the universe is to domain (that is, node, zone, or region) outages, whether planned or unplanned. Resilience is achieved by adding redundancy, in the form of additional nodes, across the fault domain. Due to the way the Raft protocol works, providing a [fault tolerance](../../../architecture/key-concepts/#fault-tolerance) of `ft` requires replicating data across `2ft + 1` domains. For example, to survive the outage of 2 nodes, a cluster needs 2 * 2 + 1 nodes. While the 2 nodes are offline, the remaining 3 nodes can continue to serve reads and writes without interruption.

With a resilient universe, planned outages such as maintenance and upgrades are performed using a rolling restart, meaning your workloads are not interrupted.

When creating or modifying universe placement, you choose **Regular Cluster** (production) or **Single-Node Cluster** (development and testing only). For regular clusters, you then choose **Guided** or **Expert** mode.

| | Guided | Expert |
| :--- | :--- | :--- |
| Best for | Most topologies | Custom layouts that Guided does not support |
| You start with | Resilience (region, zone, node, or none) and how many of those outages to tolerate | Regions, then [replication factor](../../../architecture/docdb-replication/replication/#replication-factor) |
| Replication factor | Applied automatically from the resilience you choose (RF 3, 5, or 7; or RF 1 for None) | You set RF to 1, 3, 5, or 7 |
| Node counts | The same in every availability zone | Can differ per availability zone |
| Resilience | You choose the outage domain; YBA constrains regions, zones, and nodes to match | Inferred from your regions, zones, nodes, and RF |

Guided is recommended for most users. Use Expert when you need different node counts per zone, a region and zone layout that Guided does not allow, or to set replication factor directly.

{{< note title="Classic UI" >}}

The Classic UI does not have Guided or Expert mode. You set regions, replication factor, and per-zone node counts directly, similar to Expert mode. Refer to [Create universes (Classic UI)](../create-universe-multi-zone/).

{{< /note >}}

#### Guided mode

In Guided mode, start by selecting the cluster's resilience and the number of outages (1, 2, or 3) you want the cluster to tolerate without downtime. YBA then requires a matching number of regions, availability zones, and nodes, and applies replication factor automatically (`RF = 2 × outages + 1`).

| Resilience | Resilient to | Minimum placement | RF |
| :--- | :--- | :---: | :---: |
| **Region** | 1, 2, or 3 region outages | 3, 5, or 7 nodes across 3, 5, or 7 regions | 3, 5, or 7 |
| **Availability zone** | 1, 2, or 3 zone outages | 3, 5, or 7 nodes across 3, 5, or 7 availability zones | 3, 5, or 7 |
| **Node** (Pod) | 1, 2, or 3 node outages | 3, 5, or 7 nodes in a single availability zone | 3, 5, or 7 |
| **None** | No outages | 1 node | 1 |

On Kubernetes, node-level resilience is labeled **Pod**.

All availability zones have the same number of nodes. You cannot set per-zone counts in Guided mode; switch to Expert if you need that.

After you set resilience, select regions, then assign availability zones and the number of nodes per zone. For multi-region clusters, optionally rank [preferred](#preferred-region) regions.

##### Region

- YugabyteDB can continue to do reads and writes even in case of a cloud region outage.
- Requires exactly 3, 5, or 7 regions (matching the outage count you chose). You cannot use 2, 4, or 6 regions, or more than 7.
- Each region typically contributes one availability zone; you cannot add extra zones in Guided region-level placement.
- Recommended for production deployments that must survive a region outage.

##### Availability zone

- YugabyteDB can continue to do reads and writes even in case of a cloud availability zone outage.
- Requires exactly 3, 5, or 7 availability zones, in one or more regions. You cannot select more regions than the required zone count.
- Not resilient to region outages (unless your zones happen to span enough regions to survive a region failure — Expert can express some of those layouts more directly).
- Recommended for production deployments that must survive a data center or zone outage.

Because cloud providers typically provide only 3–4 availability zones per region, surviving more than one zone outage in a _single_ region usually requires placing zones in additional regions.

##### Node

- YugabyteDB can continue to do reads and writes even in case of node outage, but this configuration is not resilient to availability zone or region outages.
- Exactly one region and one availability zone.
- Minimum of 3, 5, or 7 nodes in that zone (matching the outage count).
- On Kubernetes, this is pod-level resilience.

##### None

- Minimum of 1 node, with no replication.
- Operations that require a restart result in downtime (no rolling restart is possible).
- For development and testing only.

**Single-Node Cluster** is a separate option on the Placement page (not a Guided resilience type). It also deploys a single node with RF 1 and skips Guided and Expert mode.

#### Expert mode

In Expert mode, start by selecting one or more regions, then set the [replication factor](../../../architecture/docdb-replication/replication/#replication-factor) and place nodes in availability zones. YBA infers resilience from the combination of regions, zones, nodes, and RF (for example, RF 3 across 3 regions is region-level; RF 3 across 3 zones in one region is zone-level; RF 3 in a single zone is node-level).

Expert mode gives you more control, with the following rules:

- RF must be 1, 3, 5, or 7. RF 1 is not resilient to outages and is subject to downtime during operations that require a restart.
- RF must be greater than or equal to the number of regions you selected. For example, 3 regions requires RF 3, 5, or 7.
- You cannot place more availability zones than the RF. To add more zones, increase RF first.
- Total nodes must be at least the RF. RF _N_ requires either _N_ availability zones or at least _N_ total nodes across zones.
- You can set a different node count in each availability zone.
- You must select an availability zone for every zone row; blank zone selections are not allowed.
- Maximum RF is 7, so you cannot place nodes in more than 7 regions.

Use Expert when Guided cannot represent the topology you need — for example, different node counts per zone, two regions with zones distributed in a way Guided does not allow, or setting RF independently of a Guided resilience preset.

If you switch from Expert to Guided and the current placement is not a Guided-supported topology (for example, uneven node counts per zone), YBA warns you and **resets** the placement configuration.

#### Preferred region

You can optionally designate regions or availability zones (ranked in order of preference) as preferred. The preferred locations handle all read and write requests from clients.

Designating a region as preferred can reduce the number of network hops needed to process requests. For lower latencies and best performance, set the region closest to your application as preferred. If your application uses a smart driver, set the [topology keys](/stable/develop/drivers-orms/smart-drivers/#topology-aware-load-balancing) to target the preferred region.

When no region is preferred, YugabyteDB distributes requests equally across regions. You can set or change the preferred regions after universe creation.

Regardless of the preferred region setting, data is replicated across all the regions in the cluster to ensure the fault tolerance you configured.

You can enable [follower reads](../../../explore/going-beyond-sql/follower-reads-ysql/) to serve reads from non-preferred regions.

In cases where the cluster has read replicas and a client connects to a read replica, reads are served from the replica; writes continue to be handled by the preferred region.

#### Dedicated masters

For universes with many databases or very large table counts, place YB-Master processes on dedicated nodes. The number of master nodes is equal to the replication factor.

Dedicated master placement is available for AWS, GCP, Azure, and on-premises; it is not supported on Kubernetes. Refer to [Dedicated YB-Masters](../dedicated-master/).

#### Changing placement

After the universe is created, you can change placement (regions, zones, and nodes). Refer to [Scale universes](../../scale-deployments/edit-universe/).

The following limitations apply when editing an existing universe:

- You can increase replication factor (or Guided resilience that increases RF), but you _cannot decrease_ it. Increasing RF may also require more nodes or availability zones. Contact {{% support-platform %}} for help with capacity planning.
- If the current placement is not a topology Guided supports, Guided mode is unavailable and you must use Expert.

Read replicas and other advanced placement options are configured after the primary universe is created. Refer to [Add a read replica](../read-replicas/).

### Hardware

The size of the universe is based on the instance type (vCPUs, memory, and disk) and the number of nodes. For the universe to be [resilient](#placement), you need a minimum of 3 nodes.

For production universes, a minimum of 3 nodes with 4 to 8 vCPUs per node is recommended.

During an update, one node is always offline. When sizing your universe to your workload, ensure you have enough additional capacity to support rolling updates with minimal impact on application performance.

If your configuration doesn't match your performance requirements, you can [scale your universe](../../scale-deployments/edit-universe/) after it is created. You can increase the number of vCPUs per node (scale up, also referred to as vertical scaling), as well as the total number of nodes (scale out, also referred to as horizontal scaling). You can also increase the disk size per node. In some cases, instance type and disk size changes can be applied as a [smart resize](../../scale-deployments/edit-universe/#smart-resize) without migrating data.

YugabyteDB recommends vertical scaling until nodes have 16 vCPUs, and horizontal scaling once nodes have 16 vCPUs. For example, for a 3-node universe with 4 vCPUs per node, scale up to 8 vCPUs rather than adding a fourth node. For a 3-node universe with 16 vCPUs per node, scale out by adding a 4th node.

When creating the universe, you also choose CPU architecture (x86 or ARM) and, for cloud providers, the Linux version from the provider configuration's catalog. For airgapped installations, use a custom image; you cannot use YBA-managed Linux versions. Refer to [Create universes](../create-universes-wizard/#hardware).

For Kubernetes, set cores, memory, and volume size for TServer and Master separately rather than selecting a cloud instance type.

### YugabyteDB version

Universes are created using a YugabyteDB release that is available in your YugabyteDB Anywhere instance. You choose the version when you create the universe.

Use a version from the [stable release series](../../../releases/versioning/#stable-releases), and prefer an [LTS release](../../../releases/versioning/#stable-release-support-policy) for production.

If the version you want is not listed, import it into YugabyteDB Anywhere. Refer to [Manage YugabyteDB releases](../../manage-deployments/ybdb-releases/).

You manage upgrades. For multi-node universes, YBA performs a [rolling upgrade](../../manage-deployments/upgrade-software/) without downtime. For production universes, validate upgrades on a [staging universe](#staging-universe) first.

### Staging universe

Use a staging universe for the following tasks:

- Verifying that your application is compatible with [database updates](../../manage-deployments/upgrade-software/).
- Ensuring that your application correctly handles a rolling restart of the database without errors.
- Testing new features. Use your staging (also known as development, testing, pre-production, or canary) environment to try out new database features while your production systems are still running a previous version.
- Testing scaling operations and disaster recovery. Find out how your environment responds to a scaling operation, outages, or the loss of a node.

Create a staging universe and configure your staging environment to connect to it. The staging universe can be smaller than your production universe, but you need to ensure that it has enough resources and capacity to handle a reasonable load. Single-node and RF 1 universes are too limited for staging.

When you plan an upgrade, upgrade and validate the staging universe _before_ upgrading production. If you identify a performance problem or regression, postpone the production upgrade and contact {{% support-platform %}}.

### Backups

YugabyteDB Anywhere does not back up universes until you configure [backup storage](../../back-up-restore-universes/configure-backup-storage/) and a [backup schedule](../../back-up-restore-universes/schedule-data-backups/). Do this before going to production.

You can also perform [on-demand backups](../../back-up-restore-universes/back-up-universe-data/), [incremental backups](../../back-up-restore-universes/back-up-universe-data/#create-incremental-backups), and [point-in-time recovery](../../back-up-restore-universes/pitr/).

Don't schedule backups during maintenance windows or periods of heavy traffic. Refer to [Back up universes](../../back-up-restore-universes/).

For region-level disaster recovery, use [xCluster Disaster Recovery](../../back-up-restore-universes/disaster-recovery/) in addition to backups.

### Security

YugabyteDB Anywhere universes are not automatically exposed to the public internet. Restrict access so that only YBA, application servers, and database administrators can reach universe nodes. Refer to [Networking](../../prepare/networking/).

When you create a universe, configure the following:

- **Network access**. Optionally assign public IPs (AWS, GCP, or Azure). For Kubernetes, you can enable IPv6.
- **Encryption in transit**. Encrypt node-to-node and client-to-node traffic using a YBA-generated or customer-managed certificate. Client-to-node encryption requires node-to-node encryption. Refer to [Encryption in transit](../../security/enable-encryption-in-transit/).
- **Encryption at rest**. Encrypt universe data using a [KMS configuration](../../security/create-kms-config/aws-kms/) (AWS, GCP, Azure, or Hashicorp Vault). Refer to [Encryption at rest](../../security/enable-encryption-at-rest/).
- **Database authentication**. Enable YSQL and YCQL authentication and set the admin password. Save the password; it is not stored in YugabyteDB Anywhere.

    After the universe is provisioned, [add users](../../security/authorization-platform/) and restrict their access. Refer to [Database authorization](../../security/authorization-platform/).

    YugabyteDB Anywhere also supports LDAP and OIDC for database authentication. Refer to [Database authentication](../../security/authentication/).

## Next steps

- [Create a universe](../create-universes-wizard/)
