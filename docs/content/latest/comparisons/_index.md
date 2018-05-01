---
title: Comparisons
linkTitle: Comparisons
description: Comparisons
image: /images/section_icons/index/comparisons.png
headcontent: This page highlights how YugaByte DB compares against other operational databases in the NoSQL and distributed SQL categories. Click on the database name in the table header to see a more detailed comparison.
aliases:
  - /comparisons/
menu:
  latest:
    identifier: comparisons
    weight: 1070
---

## NoSQL Databases

Feature | [Apache Cassandra](cassandra/) | [Redis](redis/) | [MongoDB](mongodb/) | [Apache HBase](hbase/) |AWS DynamoDB | [MS Azure CosmosDB](azure-cosmos/)| YugaByte DB
--------|-----------|-------|---------|--------|-------------|--------------|-----------------
Linear Read &amp; Write Scalability | <i class="fa fa-check"></i> |<i class="fa fa-times"></i>| <i class="fa fa-check"></i> |<i class="fa fa-check"></i>| <i class="fa fa-check"></i> | <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
Automated Failover &amp; Repair | <i class="fa fa-check"></i> |<i class="fa fa-times"></i>| <i class="fa fa-check"></i> |<i class="fa fa-check"></i>|<i class="fa fa-check"></i> | <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
Auto Sharding | <i class="fa fa-check"></i> |<i class="fa fa-times"></i>|<i class="fa fa-check"></i> |<i class="fa fa-check"></i>| <i class="fa fa-check"></i> | <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
Auto Rebalancing | <i class="fa fa-check"></i> |<i class="fa fa-times"></i>| <i class="fa fa-check"></i> |<i class="fa fa-check"></i>|<i class="fa fa-check"></i> | <i class="fa fa-check"></i> | <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
Distributed ACID Transactions | <i class="fa fa-times"></i> |<i class="fa fa-times"></i>| <i class="fa fa-times"></i> |<i class="fa fa-times"></i>| <i class="fa fa-times"></i> | <i class="fa fa-times"></i> | <i class="fa fa-check"></i>
Consensus Driven Strongly Consistent Replication | <i class="fa fa-times"></i> |<i class="fa fa-times"></i>| <i class="fa fa-times"></i> |<i class="fa fa-times"></i>| <i class="fa fa-times"></i> | <i class="fa fa-times"></i> | <i class="fa fa-check"></i>
Strongly Consistent Secondary Indexes | <i class="fa fa-times"></i> |<i class="fa fa-times"></i>| <i class="fa fa-times"></i> |<i class="fa fa-times"></i>| <i class="fa fa-times"></i> | <i class="fa fa-times"></i> | <i class="fa fa-check"></i>
Multiple Read Consistency Levels | <i class="fa fa-check"></i> |<i class="fa fa-times"></i>| <i class="fa fa-check"></i> |<i class="fa fa-times"></i>| <i class="fa fa-check"></i> | <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
Low, Predictable p99 Latencies | <i class="fa fa-times"></i> |<i class="fa fa-check"></i>| <i class="fa fa-times"></i> |<i class="fa fa-times"></i>|<i class="fa fa-check"></i> | <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
High Data Density| <i class="fa fa-times"></i> |<i class="fa fa-times"></i>| <i class="fa fa-times"></i> |<i class="fa fa-check"></i>| <i class="fa fa-times"></i> | <i class="fa fa-times"></i> | <i class="fa fa-check"></i>
Cloud-Native Reconfigurability | <i class="fa fa-times"></i> |<i class="fa fa-times"></i>| <i class="fa fa-times"></i> |<i class="fa fa-times"></i>| <i class="fa fa-check"></i> | <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
Open API | CQL   |Redis| MongoQL |HBase| Proprietary | CQL, MongoQL | Cassandra-compatible YCQL, Redis-compatible YEDIS
Open Source | Apache 2.0 | 3-Clause BSD| AGPL 3.0 | Apache 2.0| <i class="fa fa-times"></i> | <i class="fa fa-times"></i> | Apache 2.0


## Distributed SQL Databases

Feature |  Clustrix | CockroachDB | AWS Aurora | [MS Azure CosmosDB](azure-cosmos/) | [Google Spanner](google-spanner/) | YugaByte DB
--------|---------|-------------|------------|----------------|----------------|-------------
Linear Write Scalability | <i class="fa fa-check"></i> |  <i class="fa fa-check"></i> | <i class="fa fa-times"></i> |<i class="fa fa-check">| <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
Linear Read Scalability | <i class="fa fa-check"></i> |  <i class="fa fa-check"></i> | <i class="fa fa-check"></i> |<i class="fa fa-check">| <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
Automated Failover &amp; Repair| <i class="fa fa-check"></i>| <i class="fa fa-check"></i> | <i class="fa fa-times"></i> |<i class="fa fa-check">| <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
Auto Sharding  |<i class="fa fa-check"></i>| <i class="fa fa-check"></i> | <i class="fa fa-times"></i> |<i class="fa fa-check">| <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
Auto Rebalancing |<i class="fa fa-check"></i>| <i class="fa fa-check"></i> | <i class="fa fa-times"></i> |<i class="fa fa-check">| <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
Distributed ACID Transactions |<i class="fa fa-check"></i>| <i class="fa fa-check"></i> | <i class="fa fa-check"></i> |<i class="fa fa-times"></i>| <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
SQL Joins|<i class="fa fa-check"></i>| <i class="fa fa-check"></i> | <i class="fa fa-check"></i> |<i class="fa fa-times"></i>| <i class="fa fa-check"></i> | <i class="fa fa-times"></i>
Consensus Driven Strongly Consistent Replication |<i class="fa fa-check"></i>| <i class="fa fa-check"></i> | <i class="fa fa-times"></i> |<i class="fa fa-times">| <i class="fa fa-check"></i> |<i class="fa fa-check"></i>
Global Consistency Across Multi-DC/Regions |<i class="fa fa-times"></i>| <i class="fa fa-check"></i> | <i class="fa fa-times"></i> |<i class="fa fa-times">| <i class="fa fa-check"></i> |<i class="fa fa-check"></i>
Multiple Read Consistency Levels | <i class="fa fa-times"></i> |<i class="fa fa-times"></i>| <i class="fa fa-times"></i> |<i class="fa fa-check"></i>| <i class="fa fa-times"></i> | <i class="fa fa-check"></i>
Cloud-Native Reconfigurability |<i class="fa fa-times"></i>| <i class="fa fa-check"></i> | <i class="fa fa-check"></i> |<i class="fa fa-check">| <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
Low, Predictable p99 Latencies | <i class="fa fa-times"></i> |<i class="fa fa-times"></i>| <i class="fa fa-check"></i> |<i class="fa fa-check"></i>|<i class="fa fa-check"></i> | <i class="fa fa-check"></i> 
SQL Compatibility |MySQL| PostgreSQL | MySQL, PostgreSQL |Read Only| Read Only| PostgreSQL (BETA)
Open Source | <i class="fa fa-times"></i>| Apache 2.0 | <i class="fa fa-times"></i> | <i class="fa fa-times"></i>| <i class="fa fa-times"></i> | Apache 2.0


{{< note title="Note" >}}
The <i class="fa fa-check"></i> or <i class="fa fa-times"></i> with respect to any particular feature of a 3rd party database is based on our best effort understanding from publicly available information. Readers are always recommended to perform their own independent research to understand the finer details.
{{< /note >}}

