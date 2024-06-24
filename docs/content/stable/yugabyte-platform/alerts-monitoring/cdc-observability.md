---
title: Change data capture (CDC) Observability in YugabyteDB Anywhere
headerTitle: CDC Observability
linkTitle: CDC Observability
description: Learn how YugabyteDB Anywhere monitors and capture and emit database change events for better visibility and insights into data changes.
headcontent: Use Change Data Capture (CDC) replication slots to capture and emit database change events.
menu:
  stable_yugabyte-platform:
    parent: alerts-monitoring
    identifier: change-data-capture
    weight: 70
type: docs
---

YugabyteDB Anywhere (YBA) supports monitoring [YSQL](../../../api/ysql/) replication slots in YugabyteDB.
You can view all the replications that are present in a universe along with the following metrics associated with each CDC replication slot:

- Current lag
- Time to expire
- Messages emitted
- Bytes emitted

The following table describe metrics available via the YugabyteDB Anywhere UI.

| Metric Name | Details |
| :---------- | :------ |
| cdcsdk_sent_lag_micros | Current lag. Lag between the last committed record in the producer and last sent record. |
| cdcsdk_expiry_time_ms | Time to expire. Remaining expiry time of CDC replication slot in milliseconds. |
| cdcsdk_change_event_count | Messages emitted. The change event count metric shows the number of records sent by the CDC service.|
| cdcsdk_traffic_sent | Bytes emitted. Total traffic sent in bytes from the CDC replication slot. |

To view the replication slots present in your universe, navigate to **Universes**, select your universe, and then click the **CDC Replication Slots** tab.

![Replication slots](/images/yp/alerts-monitoring/cdc/replication-slots1.png)

The CDC replication slots page contains a list of all replications, database names, and slot status. You can also view detailed metrics by clicking the CDC replication slot row as per the following illustration:

![Replication slot row](/images/yp/alerts-monitoring/cdc/replication-slots2.png)

## Limitation

- Currently, YugabyteDB supports CDC replication slots only for YSQL databases.

## Learn more

- Explore [Change data capture](../../../explore/change-data-capture/)
- Architecture reference of [Change data capture](../../../architecture/docdb-replication/change-data-capture/)
- [CDC Tutorials](/preview/tutorials/cdc-tutorials/cdc-redpanda/)