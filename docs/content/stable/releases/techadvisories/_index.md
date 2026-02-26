---
title: YugabyteDB Technical Advisories
headerTitle: Technical advisories
linkTitle: Technical advisories
description: List of technical advisories
image: fa-sharp fa-thin fa-triangle-exclamation
type: indexpage
showRightNav: false
cascade:
  unversioned: true
---

Review the following important information that may impact the stability or security of production deployments.

It is strongly recommended that you take appropriate measures as outlined in the advisories to mitigate potential risks and disruptions.

For an RSS feed of all technical advisories, point your feed reader to the [RSS feed for technical advisories](index.xml).

## List of advisories

{{%table%}}
| Advisory&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; | Synopsis | Product | Affected Versions | Date |
| :------------------------------- | :------- | :-----: | :---------------: | :--- |
| {{<ta 30104>}}
| Index inconsistency with Insert On Conflict with partial or expression index
| {{<product "ysql">}}
| {{<release "2024.2.0">}} to {{<release "2024.2.7.2">}}, {{<release "2025.1.0">}} to {{<release "2025.1.3.0">}}, {{<release "2025.2.0">}}
| {{<nobreak "11 February 2026">}}
|
| {{<ta 24313>}}
| YSQL online index backfill consistency issue
| {{<product "ysql">}}
| {{<release "2.14, 2.16, 2.18, 2.20, 2024.1, 2024.2.0">}}
| {{<nobreak "10 December 2025">}}
|
| {{<ta 29060>}}
| Rows larger than 4MB can cause CDC to miss WAL records
| {{<product "cdc">}}
| {{<release "2.20.7">}} to {{<release "2.20.12">}}, {{<release "2024.1.3">}}+, {{<release "2024.2.0">}} to {{<release "2024.2.6">}}, {{<release "2025.1.0">}} to {{<release "2025.1.2">}}
| {{<nobreak "24 November 2025">}}
|
| {{<ta 28267>}}
|Index inconsistency during concurrent expression index creation
| {{<product "ysql">}}
| {{<release "2024.2.0">}} to {{<release "2024.2.6">}}, {{<release "2025.1.0, 2025.1.1">}}
| {{<nobreak "17 November 2025">}}
|
| {{<ta 27177>}}
|PostgreSQL logical replication breaks during v2025.1 upgrade
| {{<product "cdc">}}
| {{<release "2025.1.0">}}
| {{<nobreak "03 October 2025">}}
|
| {{<ta 26666>}}
|Memory leak in workloads with foreign keys or serializable reads
| {{<product "ysql, ycql">}}
| {{<release "2024.2">}}
| {{<nobreak "02 September 2025">}}
|
| {{<ta 28222>}}
|Regression for queries with BNL plans
| {{<product "ysql">}}
| {{<release "2024.1, 2024.2, 2025.1">}}
| {{<nobreak "18 August 2025">}}
|
| {{<ta 2968>}}
|Import schema fails on all Voyager installs done after August 14, 2025
| {{<product "voyager">}}
| {{<release "All">}}
| {{<nobreak "15 August 2025">}}
|
| {{<ta 26440>}}
|Transparent Huge Pages (THP) causing memory issues and RSS increase
| {{<product "ybdb,yba">}}
| {{<release "All">}}
| {{<nobreak "12 August 2025">}}
|
| {{<ta 25106>}}
|TServer fatals with schema packing not found error during DDL operations
| {{<product "ysql">}}
| {{<release "2.20, 2024.1, 2024.2">}}
| {{<nobreak "04 June 2025">}}
|
| {{<ta 27380>}}
|Missing rows on xCluster target on range-sharded tables with multiple tablets
| {{<product "ysql">}}
| {{<release "2.20.x, 2024.1, 2024.2">}}
| {{<nobreak "03 June 2025">}}
|
| {{<ta 14696>}}
|DDL Atomicity health check alert shows incorrect results in YugabyteDB Anywhere
| {{<product "yba">}}
| {{<release "2.20.8.1, 2024.1.3.1, 2024.2.0.0">}}, [v2.23.1.0](/stable/releases/ybdb-releases/end-of-life/v2.23/#v2.23.1.0)
| {{<nobreak "19 February 2025">}}
|
| {{<ta 25193>}}
|Logical replication CDC may fail to stream UPDATE or DELETE changes with specific configurations
| {{<product "ysql">}}
| {{<release "2.20, 2024.1, 2024.2">}}
| {{<nobreak "10 January 2025">}}
|
| {{<ta 24992>}}
|Partial streaming stall after tablet split
| [YugabyteDB gRPC (Debezium) Connector](/stable/additional-features/change-data-capture/using-yugabytedb-grpc-replication/debezium-connector-yugabytedb/)
| [dz.1.9.5.yb.grpc.2024.1](https://github.com/yugabyte/debezium-connector-yugabytedb/releases/tag/vdz.1.9.5.yb.grpc.2024.1)
| {{<nobreak "3 December 2024">}}
|
| {{<ta 23476>}}
|YCQL timestamp precision (Millis vs Micros) issues
| {{<product "ycql">}}
| {{<release "All">}}
| {{<nobreak "21 November 2024">}}
|
| {{<ta CL-23623>}}
|Upgrade failure from v2.20 to v2024.1 series
| {{<product "ybdb">}}
| {{<release "2.20.6.x">}}
| {{<nobreak "02 October 2024">}}
|
| {{<ta 22935>}}
|Potential issues with server-side sequence caching in multi-database clusters
| {{<product "ysql">}}
| [v2.18.0.0](/stable/releases/ybdb-releases/end-of-life/v2.18/#v2.18.0.0), {{<release "2.20.0.0, 2024.1.0.0">}}
| {{<nobreak "25 June 2024">}}
|
| {{<ta 22802>}}
|Inconsistencies between system catalog and DocDB schema during DDL operations
| {{<product "ysql">}}
| [v2.14.0.0](/stable/releases/ybdb-releases/end-of-life/v2.14/#v2.14.0.0), [v2.16.0.0](/stable/releases/ybdb-releases/end-of-life/v2.16/#v2.16.0.0), [v2.18.0.0](/stable/releases/ybdb-releases/end-of-life/v2.18/#v2.18.0.0), {{<release "2.20.0.0">}}
| {{<nobreak "11 June 2024">}}
|
| {{<ta 22057>}}
|Risk of data loss when upgrading to or from version 2.20.3.0 or 2.21.0
| {{<product "ysql">}}
| {{<release "2.20.3.0">}}, [v2.21.0](/stable/releases/ybdb-releases/end-of-life/v2.21/#v2.21.0.0)
| {{<nobreak "06 May 2024">}}
|
| {{<ta REOL-24>}}
|Replicated End of Life
| {{<product "yba">}}
| {{<release "All">}}
| {{<nobreak "30 Apr 2024">}}
|
| {{<ta 21297>}}
|Missing writes during batch execution in a transaction
| {{<product "ysql">}}
| {{<release "All">}}
| {{<nobreak "26 Mar 2024">}}
|
| {{<ta 21491>}}
|Failure of upgrades to release versions 2.18 and 2.20
| {{<product "ybdb,yba">}}
| [v2.18](/stable/releases/ybdb-releases/end-of-life/v2.18/#v2.18.0.0), {{<release "2.20">}}
| {{<nobreak "19 Mar 2024">}}
|
| {{<ta 21218>}}
|DML and DDL operations fail on a colocated table with Packed Rows
| {{<product "ysql">}}
| {{<release "2.20.x">}}
| {{<nobreak "12 Mar 2024">}}
|
| {{<ta 20864>}}
|Failure of foreign key checks
| {{<product "ysql">}}
| {{<release "All">}}
| {{<nobreak "27 Feb 2024">}}
|
| {{<ta 20827>}}
|Query correctness issue with SELECT DISTINCT and BatchNestedLoopJoin
| {{<product "ysql">}}
| {{<release "2.20.1.x">}}
| {{<nobreak "06 Feb 2024">}}
|
| {{<ta 20398>}}
|Slow execution of copy command and multi-row inserts
| {{<product "ysql">}}
| {{<release "2.20.0.x">}}, [v2.19.1.x](/stable/releases/ybdb-releases/end-of-life/v2.19/)
| {{<nobreak "23 Jan 2024">}}
|
| {{<ta 20648>}}
|Index update can be wrongly applied on batch writes
| {{<product "ysql">}}
| {{<release "2.20.x">}}, [v2.19.x](/stable/releases/ybdb-releases/end-of-life/v2.19/)
| {{<nobreak "23 Jan 2024">}}
|
{{%/table%}}
