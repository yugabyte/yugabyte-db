---
title: Quality of service
headerTitle: Quality of service
linkTitle: Quality of service
description: Quality of service and admission control
headcontent: Quality of service and admission control
image: /images/section_icons/api/ysql.png
menu:
  preview:
    identifier: develop-quality-of-service
    parent: develop
    weight: 585
type: indexpage
---

Quality of service (QoS) is used to ensure that your critical services (or SQL statements) achieve performance objectives, or to just keep the cluster running under heavy load. There are two scenarios where QoS becomes important:

* **Heavy cluster utilization**: In this scenario, it becomes important to keep the cluster running, while ensuring some transactions are given higher priority. This is handled by admission control.
* **Multi-tenancy**: If the cluster used by multiple tenants or services, it becomes essential to limit the resource usage of any one tenant or service. This can be done by rate limiting resources per tenant.

These are discussed below in detail.

## Admission control

YugabyteDB implements **admission control** to ensure that a heavily loaded cluster is able to stay up and running. Admission control kicks in after a connection is authenticated and authorized, and works at various stages of query processing / execution. The following controls are available to ensure quality of service.

| Type | Scope | Description |
| :--- | :--- | :--- |
| [Write-heavy workloads](write-heavy-workloads) | `CLUSTER WIDE` | In scenarios where the write throughput can be very high, ensure that the cluster does not get overloaded, resulting in an outage. |
| [Transaction priorities](transaction-priority) | `CLUSTER WIDE` | Enables fine-grained control over which transactions should be given higher priority. |
| [Connection management](limiting-connections) | `CLUSTER WIDE`<br/>`PER USER`<br/>`PER DATABASE` | Limit the number of connections that can be established in a cluster. |
