---
title: Continuous availability
headerTitle: Continuous availability
linkTitle: Continuous availability
description: Simulate fault tolerance and resilience in a local YugabyteDB database universe.
headcontent: High availability and fault tolerance
aliases:
  - /preview/explore/fault-tolerance/
  - /preview/explore/postgresql/fault-tolerance/
  - /preview/explore/fault-tolerance-macos/
image: /images/section_icons/explore/fault_tolerance.png
menu:
  preview:
    identifier: fault-tolerance
    parent: explore
    weight: 220
type: indexpage
---

YugabyteDB can continuously serve requests in the event of planned or unplanned outages, such as system upgrades and outages related to a node, availability zone, or region. YugabyteDB's [High availability](../../architecture/core-functions/high-availability/) is achieved through a combination of distributed architecture, data replication, consensus algorithms, automatic rebalancing, and failure detection mechanisms, ensuring that the database remains available, consistent, and resilient to failures of fault domains.

## Fault domains

A fault domain is a potential point of failure. Examples of fault domains would be nodes, racks, zones, or entire regions. YugabyteDB's RAFT-based replication and automatic rebalancing ensure that even if a domain fails, the database can continue to serve reads and writes without interruption. The fault tolerance of a YugabyteDB cluster determines how resilient the cluster is to domain outages. Fault tolerance is achieved by adding redundancy in the form of additional nodes across the fault domain.

### Node failure

In an RF3 cluster, a minimum of 3 nodes is required to tolerate 1 node outage, and in an RF5 cluster you need a minimum of 5 nodes for 2 node outages, and so on. Each additional node increases the resilience to node failures.

### Zone failure

An RF3 cluster can survive 1 zone outage when spread across 3 zones. This setup ensures that even if an entire zone goes down, the database can continue operating.

### Region failure

Similar to zone-level fault tolerance, but on a larger scale, involving multiple regions. This provides the highest level of protection against region-wide outages.

### Rack failure

In the case of on-prem deployments, you can consider racks as zones to make your cluster rack-aware and ensure that your cluster spread across racks can survive rack-level failures.

{{<tip title="Rack awareness">}}
To use racks as fault domains, map them to zones using the [--cloud-location](../../reference/configuration/yugabyted/#flags) flag with [yugabyted](../../reference/configuration/yugabyted), or directly using the YB-TServer [--placement-zone](../../reference/configuration/yb-tserver/#placement-zone) flag.
{{</tip>}}

## Recovery time

If a fault domain experiences a failure, an active replica is ready to take over as a new leader in a matter of seconds after the failure of the current leader and serve requests.

This is reflected in both the recovery point objective (RPO) and recovery time objective (RTO) for YugabyteDB universes:

- The RPO for the tablets in a YugabyteDB universe is 0, meaning no data is lost in the failover to another fault domain.
- The RTO for a zone outage is approximately 3 seconds, which is the time window for completing the failover and becoming operational out of the remaining fault domains.

<img src="/images/architecture/replication/rpo-vs-rto-zone-outage.png"/>

YugabyteDB also provides HA of transactions by replicating the uncommitted values, also known as [provisional records](../../architecture/transactions/distributed-txns/#provisional-records), across the fault domains.

The benefits of continuous availability extend to performing maintenance and database upgrades. You can maintain and [upgrade your universe](../../manage/upgrade-deployment/) to a newer version of YugabyteDB by performing a rolling upgrade; that is, stopping each node, upgrading the software, and restarting the node, with zero downtime for the universe as a whole.

For more information, see the following:

- [Continuous Availability with YugabyteDB video](https://www.youtube.com/watch?v=4PpiOMcq-j8)
- [Synchronous replication](../../architecture/docdb-replication/replication/)

<div class="row">
   <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="macos/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/zero_downtime.png" aria-hidden="true" />
        <div class="title">High availability during node and zone failures</div>
      </div>
      <div class="body">
        Continuously serve requests in the event of unplanned outages.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="transaction-availability/">
      <div class="head">
        <img class="icon" src="/images/section_icons/architecture/distributed_acid.png" aria-hidden="true" />
        <div class="title">High availability of transactions</div>
      </div>
      <div class="body">
        YugabyteDB transactions survive common failure scenarios.
      </div>
    </a>
  </div>

</div>
