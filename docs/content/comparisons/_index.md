---
title: Comparisons
type: page
weight: 1070
---

This page highlights how YugaByte DB compares against other operational databases in the NoSQL and distributed SQL categories. Click on the database name in the table header to see a more detailed comparison. 

## NoSQL Databases

Feature | [Apache Cassandra](/comparisons/cassandra/) | [Redis](/comparisons/redis/) | [MongoDB](/comparisons/mongodb/) | [Apache HBase](/comparisons/hbase/) |AWS DynamoDB | MS Azure CosmosDB| YugaByte DB
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
Open API | CQL   |Redis| MongoQL |HBase| Proprietary | CQL, MongoQL | CQL, Redis
Open Source | Apache 2.0 | 3-Clause BSD| AGPL 3.0 | Apache 2.0| <i class="fa fa-times"></i> | <i class="fa fa-times"></i> | Apache 2.0


## Distributed SQL Databases

Feature |  Clustrix | CockroachDB | AWS Aurora | MS Azure CosmosDB | Google Spanner | YugaByte DB
--------|---------|-------------|------------|----------------|----------------|-------------
Linear Write Scalability | <i class="fa fa-check"></i> |  <i class="fa fa-check"></i> | <i class="fa fa-times"></i> |<i class="fa fa-check">| <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
Linear Read Scalability | <i class="fa fa-check"></i> |  <i class="fa fa-check"></i> | <i class="fa fa-check"></i> |<i class="fa fa-check">| <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
Automated Failover &amp; Repair| <i class="fa fa-check"></i>| <i class="fa fa-check"></i> | <i class="fa fa-times"></i> |<i class="fa fa-check">| <i class="fa fa-times"></i> | <i class="fa fa-check"></i>
Auto Sharding  |<i class="fa fa-check"></i>| <i class="fa fa-check"></i> | <i class="fa fa-times"></i> |<i class="fa fa-check">| <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
Auto Rebalancing |<i class="fa fa-check"></i>| <i class="fa fa-check"></i> | <i class="fa fa-times"></i> |<i class="fa fa-check">| <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
Distributed ACID Transactions |<i class="fa fa-check"></i>| <i class="fa fa-check"></i> | <i class="fa fa-check"></i> |<i class="fa fa-times"></i>| <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
SQL Joins|<i class="fa fa-check"></i>| <i class="fa fa-check"></i> | <i class="fa fa-check"></i> |<i class="fa fa-times"></i>| <i class="fa fa-check"></i> | <i class="fa fa-times"></i>
Consensus Driven Strongly Consistent Replication |<i class="fa fa-check"></i>| <i class="fa fa-check"></i> | <i class="fa fa-times"></i> |<i class="fa fa-check">| <i class="fa fa-check"></i> |<i class="fa fa-check"></i>| <i class="fa fa-check"></i>
Global Consistency Across Multi-DC/Regions |<i class="fa fa-times"></i>| <i class="fa fa-check"></i> | <i class="fa fa-times"></i> |<i class="fa fa-times">| <i class="fa fa-check"></i> |<i class="fa fa-check"></i>
Multiple Read Consistency Levels | <i class="fa fa-times"></i> |<i class="fa fa-times"></i>| <i class="fa fa-times"></i> |<i class="fa fa-check"></i>| <i class="fa fa-times"></i> | <i class="fa fa-check"></i>
Cloud-Native Reconfigurability |<i class="fa fa-times"></i>| <i class="fa fa-check"></i> | <i class="fa fa-check"></i> |<i class="fa fa-check">| <i class="fa fa-check"></i> | <i class="fa fa-check"></i>
Low, Predictable p99 Latencies | <i class="fa fa-times"></i> |<i class="fa fa-times"></i>| <i class="fa fa-check"></i> |<i class="fa fa-check"></i>|<i class="fa fa-check"></i> | <i class="fa fa-check"></i> 
SQL Compatibility |MySQL| PostgreSQL | MySQL, PostgreSQL |Read Only| Read Only| NoSQL APIs today, PostgreSQL soon
Open Source | <i class="fa fa-times"></i>| Apache 2.0 | <i class="fa fa-times"></i> | <i class="fa fa-times"></i>| <i class="fa fa-times"></i> | Apache 2.0


{{< note title="Note" >}}
The <i class="fa fa-check"></i> or <i class="fa fa-times"></i> with respect to any particular feature of a 3rd party database is based on our best effort understanding from publicly available information. Readers are always recommended to perform their own independent research to understand the finer details.
{{< /note >}}

