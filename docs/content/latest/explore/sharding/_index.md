---
title: Sharding Strategies
headerTitle: Sharding Strategies
linkTitle: Sharding Strategies
description: Range and Hash Sharding Strategies in YSQL
headcontent: Range and Hash Sharding Strategies in YSQL
image: /images/section_icons/explore/transactional.png
menu:
  latest:
    identifier: explore-sharding
    parent: explore
    weight: 299
---

Sharding is the process of breaking up large tables into smaller chunks called shards that are spread across multiple servers. A shard is essentially a horizontal data partition that contains a subset of the total data set, and hence is responsible for serving a portion of the overall workload. The idea is to distribute data that can’t fit on a single node onto a cluster of database nodes. Sharding is also referred to as horizontal partitioning. The distinction between horizontal and vertical comes from the traditional tabular view of a database. A database can be split vertically — storing different table columns in a separate database, or horizontally — storing rows of the same table in multiple database nodes.

User tables are implicitly managed as multiple shards by DocDB. These shards are referred to as **tablets**. The primary key for each row in the table uniquely determines the tablet the row lives in. This is shown in the figure below.

![Sharding a table into tablets](/images/architecture/partitioning-table-into-tablets.png)

{{< note title="Note" >}}
For every given key, there is exactly one tablet that owns it.
{{< /note >}}

YugabyteDB currently supports two ways of sharding data - hash (aka consistent hash) sharding and range sharding.
