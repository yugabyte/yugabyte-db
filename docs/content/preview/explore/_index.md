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

Whether you're setting up your first YugabyteDB cluster, diving into powerful APIs like YSQL and YCQL, or optimizing performance for global deployments, Explore offers hands-on guidance and deep insights. From core database operations to advanced features like distributed transactions, real-time data streaming, and robust security, Explore equips you with the tools and knowledge to harness YugabyteDB's cloud-native architecture and unlock its full potential for your mission-critical workloads.

## API compatibility and data models

{{< sections/3-boxes>}}
  {{< sections/3-box-card
    title="SQL features"
    description="PostgreSQL-compatible YSQL language features."
    buttonText="YSQL"
    buttonUrl="ysql-language-features/"
  >}}

  {{< sections/3-box-card
    title="Going beyond SQL"
    description="YugabyteDB exclusive features."
    buttonText="Connect"
    buttonUrl="going-beyond-sql/"
  >}}

  {{< sections/3-box-card
    title="YCQL features"
    description="Apache Cassandra-compatible YCQL language features."
    buttonText="YCQL"
    buttonUrl="ycql-language/"
  >}}

{{< /sections/3-boxes >}}

## Core capabilities

{{< sections/3-boxes>}}
  {{< sections/3-box-card
    title="Resiliency"
    description="Zero downtime when a node fails."
    buttonText="YSQL"
    buttonUrl="fault-tolerance/"
  >}}

  {{< sections/3-box-card
    title="Horizontal scalability"
    description="Dynamically add and remove nodes."
    buttonText="Connect"
    buttonUrl="linear-scalability/"
  >}}
  {{< sections/3-box-card
    title="Transactions"
    description="Distributed transactions and isolation levels."
    buttonText="YSQL"
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
    description="Authentication, authorization (RBAC), encryption, and more."
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
    description="Support for streaming data to Kafka."
    buttonText="Change data capture"
    buttonUrl="change-data-capture/"
  >}}

  {{< sections/3-box-card
    title="Cluster management"
    description="Using point-in-time recovery."
    buttonText="Cluster management"
    buttonUrl="cluster-management/"
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
