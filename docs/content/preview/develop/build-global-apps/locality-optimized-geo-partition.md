---
title: Locality Optimized Geo-Partitioning design pattern for global applications
headerTitle: Locality Optimized Geo-Partitioning
linkTitle: Locality Optimized Geo-Partitioning
description: Geo Partitioning for compliance in Multi-Active global applications
headcontent: Geo Partitioning for compliance in Multi-Active global applications
menu:
  preview:
    identifier: global-apps-locality-optimized-geo-partition
    parent: build-global-apps
    weight: 700
type: docs
---

Data residency laws require data about a nation's citizens or residents to be collected, processed, and/or stored inside the country. Multi-national businesses must operate under local data regulations that dictate how the data of a nation's residents must be stored within its borders. Re-architecting your storage layer and applications to support these could be a very daunting task.

## Partitioning for locality

You would want to have data of users from different countries (eg. US/Germany/India) in the same table, but just store the rows in their regions to comply with the country's data-protection laws (eg. [GDPR](https://en.wikipedia.org/wiki/General_Data_Protection_Regulation)), or to reduce latency for the users in those countries.

For this, YugabyteDB supports [Row-level geo-partitioning](../../../explore/multi-region-deployments/row-level-geo-partitioning/). This combines two well-known PostgreSQL concepts, [partitioning](../../../explore/ysql-language-features/advanced-features/partitions/), and [tablespaces](../../../explore/ysql-language-features/going-beyond-sql/tablespaces/).


## Setup

The setup is quite similar to the [Latency-Optimized Geo-Partition](./latency-optimized-geo-partition) pattern, but instead of `us-west` and `us-east`, you have to place the partitions in different geos, say US, Europe and India.

![Geo partitioned database for locality](/images/develop/global-apps/locality-optimized-geo-partition-setup.png)

This will ensure that the data belonging to the users of a country stays within the geo boundary as needed.
