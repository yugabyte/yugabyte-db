---
title: Cluster Load in YugabyteDB Anywhere
headerTitle: Cluster Load
linkTitle: Cluster Load
description: See whether a universe was overloaded, and why.
headcontent: See whether a universe was overloaded, and why
tags:
  feature: early-access
menu:
  stable_yugabyte-platform:
    parent: alerts-monitoring
    identifier: cluster-load-anywhere
    weight: 50
type: docs
---

Use **Cluster Load** to see when a universe was idle, busy, or waiting, and which queries contributed.

The chart is built from [Active Session History](../../../launch-and-manage/monitor-and-alert/active-session-history-monitor/) (ASH). The same samples are what {{% support-platform %}} uses for cluster load analysis in a [support bundle](../../troubleshoot/universe-issues/#perf-advisor-metrics).

## Before you begin

Cluster Load is part of Advanced Observability (available in v2026.1.3.0 and later), which is a licensed feature. If you do not already have a license, contact {{% support-platform %}}.

To enable Advanced Observability for the universe:

1. Open the universe and click **Actions > More > Enable Performance Monitoring**.

1. Choose **Advanced** and click **Apply changes**.

Advanced Observability also provides additional metrics. See [Advanced metrics](../../alerts-monitoring/anywhere-metrics/#advanced-metrics).

## View cluster load

Open the universe, select **Performance**, and then select **Cluster Load**.

You can switch between **Overall Load** and **Load by Database**.

### Overall Load

#### Cluster Load Overview

The bars are active sessions over time. A session can be a YSQL process or a YB-TServer thread. The line is the number of running queries.

Color shows where that time went:

| State | Description |
| :--- | :--- |
| CPU | Sampled sessions with wait event type CPU. `OnCpu_Active` is work running on a CPU. `OnCpu_Passive` is an RPC or task queued for a thread, and those samples are in this band too. Compare the height with the dashed total vCPU line. Above the line can be CPU saturation, or work queued for a thread. |
| Client | The application is pacing the PostgreSQL protocol. The driver is reading a result or sending the next command. |
| DiskIO | DocDB storage I/O on the tablet server, such as WAL append and sync, tablet files, and compaction. This is not local PostgreSQL I/O. |
| IO | Filesystem I/O inside the YSQL backend: local WAL, catalog files, temporary files, `COPY`, logical decoding, checkpoints, and similar. A large slice points at temporary files, catalog churn, or WAL on the PostgreSQL data directory, not at tablet **DiskIO**. |
| Lock | Waiting on a heavyweight lock, LWLock, or buffer pin. |
| RPCWait | Wait event type for an RPC that has not returned. That includes a YSQL backend in the TServerWait class, a tablet server waiting on another RPC (Raft replication, conflict resolution, or a transaction-status lookup), and a YCQL client waiting on DocDB or the master. A tall band can be Raft replication on the tablet servers. |
| Timeout | A deliberate sleep, such as `pg_sleep`, conflict backoff, or vacuum pacing. Not client I/O and not lock contention. |
| TServerWait | Wait event class for a YSQL backend waiting on a table, index, catalog, or transaction RPC. Every event in this class has type RPCWait. With **Group by** set to **Wait Event Type**, those samples are in the **RPCWait** band. |
| WaitOnCondition | Includes a DocDB row-level lock (`LockedBatchEntry_Lock`) and a wait for conflicting transactions to complete (`ConflictResolution_WaitOnConflictingTxns`). It also covers timing and internal coordination, such as safe time, Raft, the RocksDB scheduler, and RPC handoff. |

CPU is the signal to watch. A CPU band above the total vCPU line can mean CPU saturation or RPCs and tasks queued for a thread. Look at the queries that account for most of the band.

A tall band that is not CPU usually means a bottleneck of that kind. For the full list of wait events, see [Wait events](../../../launch-and-manage/monitor-and-alert/active-session-history-monitor/#wait-events).

You can view the chart for the whole universe, or limit it to outlier nodes.

#### Queries

Under the chart, **Queries** lists the statements that contributed most to the load in the selected time range. Filter the list to SQL, CQL, or background work.

Select a query to open its details in a new browser tab. The details include load for that query, performance metrics, the execution plan, and the tables it accessed.

One application statement often shows up as more than one active session: a YSQL process, its local tablet server, and tablet servers on other nodes that hold the rows.

### Load by Database

**Activity by Database** shows active sessions for each database over time.

Under the chart, the event load is broken out by database. Expand a database to see the queries that contributed, and select a query for the same details as on **Overall Load**.
