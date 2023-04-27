---
title: Compliance design patterns for global applications
headerTitle: Compliance design patterns for global applications
linkTitle: Compliance design patterns
description: Ensure compliance of data residency laws in  global applications
headcontent: Learn how to design global applications ensuring compliance
image: /images/section_icons/quick_start/sample_apps.png
menu:
  preview:
    identifier: global-apps-design-patterns-compliance
    parent: global-apps-design-patterns
    weight: 202
type: docs
---
{{<warning title="WORK IN PROGRESS!">}}

- Design pattern 1 - Pinning partitions to local geographies, but cons are access to data is not easy to restrict by geo
- Design pattern 2 - independent clusters + xcluster for a few tables
- Design pattern 3 - many independent clusters, with a global "locator" database
{{</warning>}}

Data residency laws require data about a nation's citizens or residents to be collected, processed, and/or stored inside the country. Multi-national businesses must operate under local data regulations that dictate how the data of a nation's residents must be stored within its borders. Re-architecting your storage layer and applications to support these could be a very daunting task.

Let's look at a few natural design patterns with YugabyteDB that you can choose from to help you comply with data residency laws with ease.

## Pinning tables to local geographies

By default, all tables are distributed across all fault zones defined for the cluster. There may be scenarios where you just want some tables to be located in specific regions/zones, have a different replication factor, or have different leader preferences.

In these cases, [tablespaces](../../../explore/ysql-language-features/going-beyond-sql/tablespaces/) come to the rescue. In YugabyteDB, the notion of [PostgreSQL](https://www.yugabyte.com/postgresql) tablespaces is extended to help define the data placement of tables for a geo-distributed deployment. This allows users to specify the number of replicas for a table or index, control placement, and get better performance. Replicating and pinning tables in specific regions can lower read latency and achieve compliance with data residency laws.

Let's consider a scenario where you have two tables, with one table containing data for users in Europe, and the other containing data for users in the US.

![All tables located in the US](/images/develop/global-apps/tablespaces-before.png)

With tablespaces, you can have table `us_users` in the US and `eu_users` just in Europe by creating appropriate table spaces and attaching tables as below.

```plpgsql
CREATE TABLESPACE europe WITH (
   replica_placement= '{ 
      "placement_blocks" : [ 
          {"cloud":"aws","region":"eu-west",   "zone":"eu-west-1a"},
          {"cloud":"aws","region":"eu-east",   "zone":"eu-east-1b"},
          {"cloud":"aws","region":"eu-central","zone":"eu-central-1c"}
   ]}');

CREATE TABLESPACE us WITH (
   replica_placement= '{ 
      "placement_blocks" : [ 
          {"cloud":"aws","region":"us-west",   "zone":"us-west-1a"},
          {"cloud":"aws","region":"us-east",   "zone":"us-east-1b"},
          {"cloud":"aws","region":"us-central","zone":"us-central-1c"}
   ]}');

CREATE TABLE eu_users(...) TABLESPACE europe;
CREATE TABLE us_users(...) TABLESPACE us;
```

This would enforce the tables to be distributed as shown in the illustration.

![Tables placed in the US and Europe correctly](/images/develop/global-apps/tablespaces.png)

{{< note title="Note" >}}
Tablespaces also work with indexes and not just tables.
{{</note>}}

## Pinning partitions to local geographies

Creating separate tables and pinning them to different geographies may not be an acceptable schema for all applications. You might want to have data of users from different countries (eg. US/Germany/India) in the same table, but just store the rows in their regions to comply with the country's data-protection laws (eg. GDPR), or to reduce latency for the users in those countries.

For this, YugabyteDB supports [Row-level geo-partitioning](../../../explore/multi-region-deployments/row-level-geo-partitioning/). This combines two well-known PostgreSQL concepts, [partitioning](../../../explore/ysql-language-features/advanced-features/partitions/), and [tablespaces](../../../explore/ysql-language-features/going-beyond-sql/tablespaces/). The idea is to partition your table according to the geo-locality, then attach each partition to a different tablespace to store the partition in a specific location.

Let's say we have a table of users, which has user data from the US and Germany. We want the data of all German users to be in Europe and the data of all US users to be located in the US. to set this up, follow these steps.

1. Create the Tablespaces

      ```plpgsql
      --  tablespace for europe data
      CREATE TABLESPACE eu WITH (
      replica_placement='{"num_replicas": 3, 
        "placement_blocks":[
          {"cloud":"aws","region":"eu-west","zone":"eu-west-1a","min_num_replicas":1},
          {"cloud":"aws","region":"eu-east","zone":"eu-east-1b","min_num_replicas":1},
          {"cloud":"aws","region":"eu-central","zone":"eu-central-1c","min_num_replicas":1}
        ]}'
      );

      --  tablespace for us data
      CREATE TABLESPACE us WITH (
      replica_placement='{"num_replicas": 3, 
        "placement_blocks":[
          {"cloud":"aws","region":"us-east","zone":"us-east-1b","min_num_replicas":1},
          {"cloud":"aws","region":"us-west","zone":"us-west-1a","min_num_replicas":1},
          {"cloud":"aws","region":"us-central","zone":"us-central-1c","min_num_replicas":1}
        ]}'
      );
      ```

1. Create the parent table with a partition clause

      ```plpgsql
      CREATE TABLE users (
        id INTEGER NOT NULL,
        geo VARCHAR,
      ) PARTITION BY LIST (geo);
      ```

1. Create child tables for each of the geos in the respective tablespaces

      ```plpgsql
      --  US partition table
      CREATE TABLE us_users PARTITION OF users (
        id, geo, 
            PRIMARY KEY (id HASH, geo))
      ) FOR VALUES IN ('US') TABLESPACE us;

      --  EUROPE partition table
      CREATE TABLE eu_users PARTITION OF users (
        id, geo,
            PRIMARY KEY (id HASH, geo))
      ) FOR VALUES IN ('EU') TABLESPACE eu;
      ```

Now, the user data will be placed in different regions as shown in the illustration.

![Region local access to partitions](/images/develop/global-apps/row-level-geo.png)

Instead of explicitly specifying values like EU/US for geo, it would be easier to use the [yb_server_region](../../../api/ysql/exprs/geo_partitioning_helper_functions/func_yb_server_region/) built-in function to get the local region information and set it as the geo column.

{{<tip>}}
For more design patterns, see  [Design Patterns](../../../../explore/transactions/isolation-levels/)
{{</tip>}}

