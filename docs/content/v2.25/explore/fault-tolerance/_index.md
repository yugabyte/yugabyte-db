---
title: Resiliency, high availability, and fault tolerance
headerTitle: Resiliency, high availability, and fault tolerance
linkTitle: Resiliency
description: Simulate fault tolerance and resilience in a local YugabyteDB database universe.
headcontent: Survive and recover from outages
aliases:
  - /preview/explore/postgresql/fault-tolerance/
  - /preview/explore/fault-tolerance-macos/
menu:
  preview:
    identifier: fault-tolerance
    parent: explore
    weight: 220
type: indexpage
showRightNav: true
---

Resiliency, in the context of cloud databases, refers to the ability to withstand and recover from various types of failures. These can range from hardware malfunctions and software bugs to network outages and natural disasters. A resilient database system is designed to maintain data integrity, accessibility, and continuity of operations, even in the face of adverse events. Achieving resilience in cloud databases requires a multi-faceted approach, involving robust architectural design, effective data replication and backup strategies, load balancing, failover mechanisms, and comprehensive monitoring and incident response procedures.

YugabyteDB has been designed from the ground up to be resilient. YugabyteDB can continuously serve requests in the event of planned or unplanned outages, such as system upgrades and outages related to a node, availability zone, or region. YugabyteDB's high availability is achieved through a combination of distributed architecture, data replication, consensus algorithms, automatic rebalancing, and failure detection mechanisms, ensuring that the database remains available, consistent, and resilient to failures of fault domains.

The following sections explore the key strategies, technologies, and best practices for enhancing the resilience of cloud databases.

## Need for resilience

The need for resilience in cloud databases arises from several factors:

- **Distributed nature**: Cloud databases are often distributed across multiple physical locations, introducing complexities in data synchronization and potential points of failure.
- **Ephemeral instances**: Virtual machines are spawned on commodity hardware which can disappear at any time. They are ephemeral.
- **Unpredictable workloads**: Cloud databases must handle fluctuating and unpredictable workloads, which can strain resources and potentially lead to performance degradation or outages. High loads can cause power surges. Backup generators have failed too.
- **Failure of fault domains**: Although public clouds have come a long way since the inception of AWS in 2006, region and zone outages are still fairly common, happening once or twice a year. Disks and machines commonly fail due to a variety of reasons.
- **Cyber attacks**: Your applications could be exposed to security threats or attacks. Resilience can help protect applications against these threats by isolating components, and responding to anomalies, and thus ensuring that such incidents do not compromise the entire system.
- **Planned upgrades**: You might have to upgrade your operating system or database, or even update your software with security patches. For this, your applications have to be taken offline.

## Fault domains

A fault domain is a potential point of failure. Examples of fault domains would be nodes, racks, zones, or entire regions. YugabyteDB's Raft-based replication and automatic rebalancing ensure that even if a domain fails, the database can continue to serve reads and writes without interruption. The fault tolerance of a YugabyteDB universe determines how resilient the universe is to domain outages. Fault tolerance is achieved by adding redundancy in the form of additional nodes across the fault domain.

### Node failure

In a universe with a replication factor (RF) of 3, the fault tolerance is equal to 1 node. That is, a minimum of 3 nodes is required to tolerate 1 node outage; an RF 5 universe needs a minimum of 5 nodes to tolerate 2 node outages. Each additional node increases the resilience to node failures.

{{<lead link="./macos/">}}
Learn how YugabyteDB [handles node failures](./macos/).
{{</lead>}}

### Rack failure

In the case of on-premises deployments, you can consider racks as zones to make your universe rack-aware and ensure that a universe spread across racks can survive rack-level failures.

{{<lead link="./handling-rack-failures/">}}
Learn how YugabyteDB [handles rack failures](./handling-rack-failures/).
{{</lead>}}

### Zone failure

An RF 3 universe can survive 1 zone outage when spread across 3 zones. This setup ensures that even if an entire zone goes down, the database can continue operating.

{{<lead link="./handling-zone-failures/">}}
Learn how YugabyteDB [handles zone failures](./handling-zone-failures/).
{{</lead>}}

### Region failure

This is similar to zone-level fault tolerance, but on a larger scale, where nodes are spread across multiple regions. This provides the highest level of protection, providing fault tolerance against region-wide outages.

{{<lead link="./handling-region-failures/">}}
Learn how YugabyteDB [handles region failures](./handling-region-failures/).
{{</lead>}}

## Planned maintenance

The benefits of continuous availability extend to performing maintenance and database upgrades. You can maintain and [upgrade your universe](../../manage/upgrade-deployment/) to a newer version of YugabyteDB by performing a rolling upgrade; that is, stopping each node, upgrading the software, and restarting the node with zero downtime for the universe as a whole. YugabyteDB manages such scenarios without any service interruption.

{{<lead link="./handling-node-upgrades/">}}
Learn how YugabyteDB continues without any service interruption during [planned node outages](./handling-node-upgrades/).
{{</lead>}}

## Transaction resilience

YugabyteDB ensures that the [provisional records](../../architecture/transactions/distributed-txns/#provisional-records) are replicated across fault domains to ensure that transactions do not fail on the failure of fault domains.

{{<lead link="./transaction-availability/">}}
Learn how YugabyteDB provides [high availability of transactions](./transaction-availability/).
{{</lead>}}

## Recovery time

If a fault domain experiences a failure, an active replica is ready to take over as a new leader in a matter of seconds after the failure of the current leader and serve requests.

This is reflected in both the recovery point objective (RPO) and recovery time objective (RTO) for YugabyteDB universes:

- The RPO for the tablets in a YugabyteDB universe is 0, meaning no data is lost in the failover to another fault domain.
- The RTO for a zone outage is approximately 3 seconds, which is the time window for completing the failover and becoming operational out of the remaining fault domains.

![RPO vs RTO](/images/architecture/replication/rpo-vs-rto-zone-outage.png)

## Learn more

- [YFTT: Continuous Availability with YugabyteDB](https://www.youtube.com/watch?v=4PpiOMcq-j8)
- [Synchronous replication](../../architecture/docdb-replication/replication/)
