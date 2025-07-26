---
title: Explore YugabyteDB
headerTitle: Explore YugabyteDB
linkTitle: Explore
headcontent: Learn about YugabyteDB features, with examples
description: Explore the features of YugabyteDB on macOS, Linux, Docker, and Kubernetes.
aliases:
  - /preview/explore/high-performance/
  - /preview/explore/planet-scale/
  - /preview/explore/cloud-native/orchestration-readiness/
type: indexpage
showRightNav: true
---

Whether you're setting up your first YugabyteDB cluster, evaluating YugabyteDB for a deployment, or want to learn more about YugabyteDB, Explore offers hands-on guidance to YugabyteDB's features, with examples. From core database operations to advanced features like distributed transactions, real-time data streaming, and robust security, Explore equips you with the tools and knowledge to harness YugabyteDB's cloud-native architecture and unlock its full potential for your mission-critical workloads.

## API compatibility and data models

{{< sections/3-boxes>}}
  {{< sections/3-box-card
    title="PostgreSQL-compatible API"
    description="Explore YSQL, YugabyteDB's fully-relational, PostgreSQL-compatible query language."
    linkText1="JSON support"
    linkUrl1="ysql-language-features/jsonb-ysql/"
    linkText2="Indexes"
    linkUrl2="ysql-language-features/indexes-constraints/"
    linkText3="Advanced features"
    linkUrl3="ysql-language-features/advanced-features/"
    linkText4="Explore more"
    linkClass4="more"
    linkUrl4="ysql-language-features/"
  >}}

  {{< sections/3-box-card
    title="Going beyond SQL"
    description="Learn about YugabyteDB's advanced features for cloud-native applications."
    linkText1="Cluster-aware client drivers"
    linkUrl1="going-beyond-sql/cluster-aware-drivers/"
    linkText2="Built-in connection pooling"
    linkUrl2="going-beyond-sql/connection-mgr-ysql/"
    linkText3="Geopartitioning"
    linkUrl3="going-beyond-sql/tablespaces/"
    linkText4="Explore more"
    linkClass4="more"
    linkUrl4="going-beyond-sql/"
  >}}

  {{< sections/3-box-card
    title="Cassandra-compatible API"
    description="Explore YCQL, YugabyteDB's semi-relational, Cassandra-compatible query language."
    linkText1="Keyspaces and tables"
    linkUrl1="ycql-language/keyspaces-tables/"
    linkText2="Data types"
    linkUrl2="ycql-language/data-types/"
    linkText3="Indexes and constraints"
    linkUrl3="ycql-language/indexes-constraints/"
    linkText4="Explore more"
    linkClass4="more"
    linkUrl4="ycql-language/"
  >}}

{{< /sections/3-boxes >}}

## Core capabilities

{{< sections/3-boxes>}}
  {{< sections/3-box-card
    title="Horizontal scalability"
    description="Learn about YugabyteDB's operationally simple and completely transparent scaling."
    buttonText="Scale"
    buttonUrl="linear-scalability/"
  >}}

  {{< sections/3-box-card
    title="Resiliency"
    description="Learn how YugabyteDB survives and recovers from outages with zero downtime."
    buttonText="Resiliency"
    buttonUrl="fault-tolerance/"
  >}}

  {{< sections/3-box-card
    title="Distributed transactions"
    description="Learn how distributed transactions work and about YugabyteDB's isolation levels."
    buttonText="Transactions"
    buttonUrl="transactions/"
  >}}

{{< /sections/3-boxes >}}

## Deployment and operations

{{< sections/3-boxes>}}

  {{< sections/3-box-card
    title="Multi-region deployments"
    description="Flexible deployment options to achieve the right mix of latency, resilience, and consistency."
    buttonText="Deploy"
    buttonUrl="multi-region-deployments/"
  >}}

  {{< sections/3-box-card
    title="Observability"
    description="Monitoring, alerting, and analyzing metrics."
    buttonText="Observability"
    buttonUrl="observability/"
  >}}

  {{< sections/3-box-card
    title="Security"
    description="YugabyteDB supports authentication, authorization (RBAC), encryption, and more."
    buttonText="Security"
    buttonUrl="security/security/"
  >}}

  {{< sections/3-box-card
    title="Colocation"
    description="Keep closely related data together via colocation."
    buttonText="Colocation"
    buttonUrl="colocation/"
  >}}

  {{< sections/3-box-card
    title="Query tuning"
    description="Identify and optimize queries in YSQL."
    buttonText="Query tuning"
    buttonUrl="query-1-performance/"
  >}}

  {{< sections/3-box-card
    title="Change data capture"
    description="Stream data to Kafka using PostgreSQL logical replication."
    buttonText="Change data capture"
    buttonUrl="change-data-capture/"
  >}}

  {{< sections/3-box-card
    title="Point-in-time recovery"
    description="Recover your database at a specific point in time."
    buttonText="PITR"
    buttonUrl="cluster-management/point-in-time-recovery-ysql/"
  >}}

{{< /sections/3-boxes >}}

<!--
| Section | Purpose | [Universe&nbsp;setup](#set-up-yugabytedb-universe) |
| :--- | :--- | :--- |
| [SQL features](ysql-language-features/) | Learn about YugabyteDB's compatibility with PostgreSQL, including data types, queries, expressions, operators, extensions, and more. | Single-node<br/>local/cloud |
| [YCQL features](ycql-language/) | Learn about YugabyteDB's Apache Cassandra-compatible YCQL language features. | Single-node<br/>local/cloud |
| [Going beyond SQL](going-beyond-sql/) | Learn about YugabyteDB exclusive features such as follower reads, tablespaces, built-in connection pooling, and more. | Multi-node<br/>local |
| [Resiliency](fault-tolerance/) | Learn how YugabyteDB achieves resiliency when a node fails. | Multi-node<br/>local |
| [Horizontal scalability](linear-scalability/) | See how YugabyteDB handles loads while dynamically adding or removing nodes. | Multi-node<br/>local |
| [Transactions](transactions/) | Understand how distributed transactions and isolation levels work in YugabyteDB. | Single-node<br/>local/cloud |
| [Colocation](colocation/) | YugabyteDB allows for closely related data to reside together via colocation. Learn how to co-locate tables. | Single-node<br/>local/cloud |
| [Multi-region deployments](multi-region-deployments/) | Learn about the different multi-region topologies that you can deploy using YugabyteDB. | Multi-node<br/>local |
| [Query tuning](query-1-performance/) | Learn about the tools available to identify and optimize queries in YSQL. | Single-node<br/>local/cloud |
| [Cluster management](cluster-management/) | Learn how to roll back database changes to a specific point in time using point-in-time recovery. | Single-node<br/>local |
| [Change data capture](change-data-capture/) | Learn about YugabyteDB support for streaming data to Kafka. | N/A |
| [Security](security/security/) | Learn how to secure data in YugabyteDB, using authentication, authorization (RBAC), encryption, and more. | Single-node<br/>local/cloud |
| [Observability](observability/) | Export metrics into Prometheus and create dashboards using Grafana. | Multi-node<br/>local |
-->
