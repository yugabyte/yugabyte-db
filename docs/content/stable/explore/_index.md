---
title: Explore YugabyteDB
headerTitle: Explore YugabyteDB
linkTitle: Explore
headcontent: Learn about YugabyteDB features, with examples
description: Explore the features of YugabyteDB on macOS, Linux, Docker, and Kubernetes.
aliases:
  - /stable/explore/high-performance/
  - /stable/explore/planet-scale/
  - /stable/explore/cloud-native/orchestration-readiness/
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

  {{< sections/3-box-card
    title="Gen-AI applications"
    description="Explore how to build Generative AI applications using YugabyteDB."
    buttonText="Gen-AI applications"
    buttonUrl="gen-ai-apps/"
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
