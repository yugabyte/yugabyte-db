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
| Advisory&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; | Synopsis | Product | Affected Versions | Date |
| :------------------------------------- | :------- | :-----: | :---------------: | :--- |

| {{<ta 21297>}}
| Missing writes during Batch Execution in a transaction
| {{<product "ysql">}}
| {{<release "All">}}
| {{<nobreak "12 Mar 2024">}}
|
| {{<ta 20827>}}
|Correctness issue for queries using SELECT DISTINCT
| {{<product "ysql">}}
| {{<release "2.20.1.x">}}
| {{<nobreak "02 Feb 2024">}}
|
| {{<ta 20648>}}
|Index update can be wrongly applied on batch writes
| {{<product "ysql">}}
| {{<release "2.20.x, 2.19.x">}}
| {{<nobreak "23 Jan 2024">}}
|
{{%/table%}}
