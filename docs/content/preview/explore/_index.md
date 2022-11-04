---
title: Explore YugabyteDB
headerTitle: Explore YugabyteDB
linkTitle: Explore
headcontent: Learn about YugabyteDB features, with examples
description: Explore the features of YugabyteDB on macOS, Linux, Docker, and Kubernetes.
image: /images/section_icons/index/explore.png
aliases:
  - /preview/explore/cloud-native/
  - /preview/explore/transactional/
  - /preview/explore/high-performance/
  - /preview/explore/planet-scale/
  - /preview/explore/cloud-native/orchestration-readiness/
type: indexpage
---

The Explore section walks you through YugabyteDB's core features, with examples. Most examples demonstrating database features such as API compatibility can be run on a single-node cluster on your laptop or using the free Sandbox cluster in YugabyteDB Managed. More advanced scenarios use a multi-node deployment of YugabyteDB.

Explore assumes that you have either created an account in YugabyteDB Managed or installed YugabyteDB on your local computer.

| Section | Purpose | Setup |
| :--- | :--- | :--- |
| [SQL features](ysql-language-features/) | Learn about YugabyteDB's wire-compatibility with PostgreSQL, including data types, queries, expressions, operators and functions, and more. | Single-node cluster<br/>Local instance or YugabyteDB Managed |}
| [Going beyond SQL](ysql-language-features/going-beyond-sql/) | Learn about reducing read latency with follower reads and moving data closer to users with tablespaces. | Multi-node cluster<br/>Local instance |}
| [Fault tolerance](fault-tolerance/macos/) | Learn how YugabyteDB achieves high availability when a node goes down. | Multi-node cluster<br/>Local instance |
| [Horizontal scalability](linear-scalability/) | See how YugabyteDB handles loads while dynamically adding or removing nodes. | Multi-node cluster<br/>Local instance |
| [Transactions](transactions/) | Understand how distributed transactions and isolation levels work in YugabyteDB. | Single-node cluster<br/>Local instance or YugabyteDB Managed |
| [Indexes and constraints](indexes-constraints/) | Explore indexes in YugabyteDB, including primary and foreign keys, secondary, unique, partial, and expression indexes, and more. | Single-node cluster<br/>Local instance or YugabyteDB Managed |
| [JSON support](json-support/jsonb-ysql/) | YugabyteDB support for JSON is nearly identical to that in PostgreSQL. Learn about JSON-specific functions and operators available in YugabyteDB. | Single-node cluster<br/>Local instance or YugabyteDB Managed |
| [Multi-region deployments](multi-region-deployments/) | Learn about the different multi-region deployment topologies that can be deployed using YugabyteDB. | Multi-node cluster<br/>Local instance |
| [Query tuning](query-1-performance/) | Learn about the tools available for identifying and optimizing queries in YSQL. | Single-node cluster<br/>Local instance or YugabyteDB Managed |
| [Cluster management](cluster-management/) | Learn how to roll back database changes to a specific point in time using point in time recovery. | Single-node cluster<br/>Local instance |
| [Change data capture](change-data-capture/) | Learn about YugabyteDB’s support for streaming data to Kafka. | N/A |
| [Security](security/security/) | Learn how YugabyteDB secures your data, using authentication, authorization (RBAC), encryption, and more. | Single-node cluster<br/>Local instance or YugabyteDB Managed |
| [Observability](observability/) | Export metrics into Prometheus and create dashboards using Grafana. | Multi-node cluster<br/>Local instance |

## Set up your environment

To run the examples in explore, you'll need to create a single- or multi-node cluster.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="#cloud" class="nav-link active" id="cloud-tab" data-toggle="tab"
       role="tab" aria-controls="cloud" aria-selected="true">
      <i class="fas fa-cloud" aria-hidden="true"></i>
      Use a cloud cluster
    </a>
  </li>
  <li>
    <a href="#local" class="nav-link" id="local-tab" data-toggle="tab"
       role="tab" aria-controls="local" aria-selected="false">
      <i class="icon-shell" aria-hidden="true"></i>
      Use a local cluster
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="cloud" class="tab-pane fade show active" role="tabpanel" aria-labelledby="cloud-tab">
  {{% includeMarkdown "setup/cloud.md" %}}
  </div>
  <div id="local" class="tab-pane fade" role="tabpanel" aria-labelledby="local-tab">
  {{% includeMarkdown "setup/local.md" %}}
  </div>
</div>
