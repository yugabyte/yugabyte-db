---
title: Monitor YugabyteDB Aeon cluster tables and tablets
headerTitle: Monitor cluster tables and tablets
linkTitle: Monitor tables
description: Monitor the size and replication status of your tables.
headcontent: Monitor the size and replication status of your tables
menu:
  stable_yugabyte-cloud:
    identifier: monitor-tables
    parent: cloud-monitor
    weight: 610
type: docs
---

Review cluster tables and their [tablets](../../../architecture/key-concepts/#tablet) on the **Tables** tab. The tab shows the number and size of your tables, as well as their health, including which tables are under-replicated.

![Cluster Tables tab](/images/yb-cloud/monitor-tables.png)

Note that table size is calculated from the sum of the write ahead logs (WAL) and sorted-string table (SST) files, across all nodes in the cluster. Changes to the database are first recorded to the WAL. Periodically, these logs are written to SST files for longer-term storage. During this process, the data is compressed. When this happens, you may observe a reduction in the total size of tables.

To view table tablets, click the table in the list. The tablets view lists the tablets and their replication status.

For more information on replication in YugabyteDB, refer to the following topics:

- [Data distribution](../../../explore/linear-scalability/data-distribution/)
- [Synchronous replication](../../../architecture/docdb-replication/replication/)
- [Tablet splitting](../../../architecture/docdb-sharding/tablet-splitting/)
