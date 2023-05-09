---
title: Performance of global applications
headerTitle: Enhance performance of global applications
linkTitle: Performance
description: Enhance the performance of global applications
headcontent: Learn how to improve the performance of your global application
image: /images/section_icons/quick_start/sample_apps.png
menu:
  preview:
    identifier: global-apps-performance
    parent: build-global-apps
    weight: 204
type: docs
---

Although public clouds have come a long way since the inception of AWS in 2006, region and zone outages are still fairly common, happening once or twice a year (cf. [AWS Outages](https://en.wikipedia.org/wiki/Timeline_of_Amazon_Web_Services#Amazon_Web_Services_outages), [Google Outages](https://en.wikipedia.org/wiki/Google_services_outages#:~:text=During%20eight%20episodes%2C%20one%20in,Google%20service%20in%20August%202013)).

YugabyteDB can be deployed in various configurations like single-region multi-zone configuration or multi-region multi-zone. Replicas are maintained across multiple fault zones to maintain high availability in case of zone outages. You can deploy YugabyteDB across different regions so that applications can handle region outages without affecting your customers. However, having a cluster spread across geographically distant locations can have some downsides, such as increased latency.

Let's look at some techniques that will help you reduce read latencies for your globally distributed cluster.

## Reducing Latency with Preferred Leaders

In YugabyteDB the table data is split into tablets. Each tablet is replicated across different fault zones and one instance is elected as the leader. This leader is responsible for handling reads and writes, and for replicating the data to its peer replicas.

Let’s consider a multi-region cluster where you have the cluster distributed across three regions: `us-east`, `us-central`, and `us-west`. Let's say we have one table with three tablets. By default, the tablet leaders are placed across the three regions in a load-balanced manner as shown in the following illustration.

![RF3 Multi-Region cluster](/images/develop/global-apps/preferred-leaders-before.png)


If you have an application running in `us-central,` it would have to reach out to leaders in the `us-east` and `us-west` to fetch the data these leaders are responsible for. Your queries would have to unnecessarily bear the latency to go from `us-central` to `us-east` or `us-central` to `us-west.`

Typically, inter-region latencies run in the order of tens of milliseconds (10-100ms). Wouldn't it be great if all the leaders were in the `us-central` region?

YugabyteDB lets you configure your cluster, so all the leaders can be placed in the region or subset of regions you choose. This is called Leader Preference. Let's place our leaders in `us-central` as that is where our application runs via the set_preferred_zones command in `yb-admin`:

```sh
yb-admin set_preferred_zones aws.us-central.us-central-1a:1
```

![RF3 with Preferred Leaders](/images/develop/global-apps/preferred-leaders.png)

As you can see, all of the leaders are in the `us-central`. The latency between the application and the tablet leaders reduces to a few milliseconds (typically 1-2 ms). Now, the `us-central` serves all data, and the rest of the regions just serve as replicas.

If your applications are running in two regions, you might want to set up leader preference, so that the tablet leaders are split across just the two regions.

But, even in this scenario, setting the leader preference to a single region would be advantageous. The internal communication would be restricted to only one region because all the leaders would be in the same region. The application would have to incur the cross-region latency only once per query, and all the internally distributed queries would be automatically restricted to just one region!

Remember, setting leader preference at the cluster level (using the `yb-admin` command) cluster affects all databases and tables in that cluster. We will see in later sections how this can be set at a finer-grained level using the concept of tablespaces.

{{<tip>}}
For more information on how to configure preferred leaders, see [set_preferred_zones](../../../admin/yb-admin/#set-preferred-zones)
{{</tip>}}

## Reducing Read Latency with Follower Reads

All reads in YugabyteDB, by default, are handled by the leader to ensure that the applications fetch the latest data, even though the data is replicated to the followers. Replication is fast, but not instantaneous. So, all followers may not have the latest data at the read time. There are a few scenarios where reading from the leader is not necessary:

- The data does not change often (for example, movie database)
- The application does not need the latest data (for example, yesterday’s report)

In such scenarios, you can enable follower reads in YugabyteDB to read from follower replicas instead of going to the leader, which could be far away in a different region. To enable this, all you have to do is set the transactions to be read-only and turn ON a configuration parameter `yb_read_from_followers` like

```plpgsql
set session characteristics as transaction read only;
SET yb_read_from_followers = true;

--  Now, this select will go to the followers
SELECT * FROM demo WHERE user='yugabyte'
```

![Follower Reads](/images/develop/global-apps/follower-reads.png)

This will read data from the closest follower or leader. As replicas may not be up-to-date with all updates, by design, this might return slightly stale data (default: 30s). This is the case even if the read goes to a leader. The staleness value can be changed using another configuration parameter `yb_follower_read_staleness_ms` like:

```plpgsql
SET yb_follower_read_staleness_ms = 10000; -- 10s
```

{{<note title="Note">}}
This is only for reads. All writes still go to the leader.
{{</note>}}


## Avoid Trips to the Table with Covering Indexes

When an index is created on some columns of a table, those columns will be a part of the index. Any lookup involving just those columns will skip the trip to the table for further data fetch. But, if the look-up involves other columns not part of the index, the query executor has to go to the actual table to fetch the columns. Consider the following example:

```plpgsql
--  Users table
CREATE TABLE users (
   id INTEGER NOT NULL,
   name VARCHAR,
   city VARCHAR,
   PRIMARY KEY(id);
);

CREATE INDEX idx_name ON users (name);

--  index scan + table lookup
SELECT id, city FROM users WHERE name='John Wick';
```

![Without covering index](/images/develop/global-apps/covering-index-before.png)

This trip could become expensive depending on the latency needs of your application. You can avoid this by creating a [covering index](../../../explore/indexes-constraints/covering-index-ysql/) that includes all the necessary columns in the index itself. This will result in an [index-only](https://www.yugabyte.com/blog/design-indexes-query-performance-distributed-database/#index-only-scan) scan.

```plpgsql
--  To get the geo of an id : Will search all partitions
CREATE INDEX idx_name ON users (name) INCLUDE (id, city);

--  index only scan
SELECT id, city FROM users WHERE name='John Wick';
```

![With covering index](/images/develop/global-apps/covering-index.png)

With the covering index, the lookups are restricted just to the index. A trip to the table is not required as the index has all the needed data.

## Identity Indexes

All reads go to the leader by default in YugabyteDB. So, even though the replicas are available in other regions, applications will have to incur cross-region latency if leaders are distributed across regions. 

We saw in an earlier section how we could circumvent the issue by having all leaders in one region. But, what if your app needs to be deployed in multiple regions and provide low latency, strongly consistent reads for all your users? To handle such situations, it's advisable to generate several identical covering indexes and store them in tablespaces that span the same set of regions but only differ in which region is the "preferred leader" region. When the schema of the index is the same as the schema of the table, it is known as an **Identity Index**.

Let’s consider the following scenario, where you have a users table present in three regions and your applications deployed in these three regions.

![RF3 with apps in multiple regions](/images/develop/global-apps/duplicate-covering-before.png)

To make the index data available in multiple regions (say `us-west,` `us-central`, `us-west` for consistent reads, follow these steps.

1. Create multiple tablespaces (one for every region you want your index leader to be located in) and set leader preference to that region:
{{<note title="Note" >}}
Even though the leader preference is set to a region, you should place the replicas in other regions for outage scenarios, depending on your application setup.
{{</note>}}

      ```plpgsql
      --  tablespace for west data
      CREATE TABLESPACE west WITH (
        replica_placement= '{ 
            "num_replicas" : 3,
            "placement_blocks" : [ 
                {"cloud":"aws","region":"us-west","zone":"us-west-1a","leader_preference": "1"}
                {"cloud":"aws","region":"us-east","zone":"us-east-1a"}
                {"cloud":"aws","region":"us-central","zone":"us-central-1a"}
        ]}');

      --  tablespace for central data
      CREATE TABLESPACE central WITH (
        replica_placement= '{ 
            "num_replicas" : 3,
            "placement_blocks" : [ 
                {"cloud":"aws","region":"us-west","zone":"us-west-1a"}
                {"cloud":"aws","region":"us-east","zone":"us-east-1a",}
                {"cloud":"aws","region":"us-central","zone":"us-central-1a","leader_preference": "1"}
        ]}');

      --  tablespace for east data
      CREATE TABLESPACE east WITH (
        replica_placement= '{ 
            "num_replicas" : 3,
            "placement_blocks" : [ 
                {"cloud":"aws","region":"us-west","zone":"us-west-1a"}
                {"cloud":"aws","region":"us-east","zone":"us-east-1a","leader_preference": "1"
                {"cloud":"aws","region":"us-central","zone":"us-central-1a"}
        ]}');
      ```

1. Create multiple covering indexes and attach them to region-level tablespaces

      ```plpgsql
      CREATE INDEX idx_west    ON users (name) INCLUDE (id, city) TABLESPACE west;
      CREATE INDEX idx_east    ON users (name) INCLUDE (id, city) TABLESPACE east;
      CREATE INDEX idx_central ON users (name) INCLUDE (id, city) TABLESPACE central;
      ```

This will create three clones of the covering index, with leaders in different regions and at the same time replicated in the other regions. You will get a setup similar to this:

![Duplicate covering indexes](/images/develop/global-apps/duplicate-covering.png)

The query planner will use the index whose leaders are local to the region when querying.
{{<note title="Note">}}
The query planner optimizations related to picking the right index by taking into consideration the leader preference of the tablespace in which the index lives are available from 2.17.3+ releases.
{{</note>}}

As you have added all of the columns needed for your queries as part of the covering index, the query executor will not have to go to the tablet leader (in a different region) to fetch the data. One important thing to note is, having multiple indexes across regions will increase write latency, as every write would have to add data into each of the indexes across regions. So, this technique is useful for lookup tables where data does not change much.

When you set up your duplicate covering indexes as identity indexes, it will have the effect of having consistent leaders for the table in each region.
