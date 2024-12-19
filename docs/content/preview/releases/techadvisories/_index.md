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

## List of advisories

{{%table%}}
| Advisory&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; | Synopsis | Product | Affected Versions | Date |
| :------------------------------- | :------- | :-----: | :---------------: | :--- |
| {{<ta 24992>}}
|Partial Streaming Stall After Tablet Split
| [YugabyteDB gRPC (Debezium) Connector](https://docs.yugabyte.com/preview/explore/change-data-capture/using-yugabytedb-grpc-replication/debezium-connector-yugabytedb/)
| [dz.1.9.5.yb.grpc.2024.1](https://github.com/yugabyte/debezium-connector-yugabytedb/releases/tag/vdz.1.9.5.yb.grpc.2024.1)
| {{<nobreak "3 December 2024">}}
|
| {{<ta 23476>}}
|YCQL Timestamp Precision (Millis vs Micros)
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
| {{<release "2.18.0.0, 2.20.0.0, 2024.1.0.0">}}
| {{<nobreak "25 June 2024">}}
|
| {{<ta 22802>}}
|Inconsistencies between system catalog and DocDB schema during DDL operations
| {{<product "ysql">}}
| {{<release "2.14.0.0">}}, [v2.16.0.0](/preview/releases/ybdb-releases/end-of-life/v2.16/#v2.16.0.0), {{<release "2.18.0.0, 2.20.0.0">}}
| {{<nobreak "11 June 2024">}}
|
| {{<ta 22057>}}
|Risk of data loss when upgrading to or from version 2.20.3.0 or 2.21.0
| {{<product "ysql">}}
| {{<release "2.20.3.0, 2.21.0">}}
| {{<nobreak "06 May 2024">}}
|
| {{<ta REOL-24>}}
|Replicated End of Life
| {{<product "yba">}}
| {{<release "All">}}
| {{<nobreak "30 Apr 2024">}}
|
| {{<ta 21297>}}
|Missing writes during Batch Execution in a transaction
| {{<product "ysql">}}
| {{<release "All">}}
| {{<nobreak "26 Mar 2024">}}
|
| {{<ta 21491>}}
|Failure of upgrades to release versions 2.18 and 2.20
| {{<product "ybdb,yba">}}
| {{<release "2.18, 2.20">}}
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
|Correctness issue for queries using SELECT DISTINCT
| {{<product "ysql">}}
| {{<release "2.20.1.x">}}
| {{<nobreak "06 Feb 2024">}}
|
| {{<ta 20398>}}
|Slow execution of copy command and multi-row inserts
| {{<product "ysql">}}
| {{<release "2.20.0.x, 2.19.1.x">}}
| {{<nobreak "23 Jan 2024">}}
|
| {{<ta 20648>}}
|Index update can be wrongly applied on batch writes
| {{<product "ysql">}}
| {{<release "2.20.x, 2.19.x">}}
| {{<nobreak "23 Jan 2024">}}
|
{{%/table%}}
