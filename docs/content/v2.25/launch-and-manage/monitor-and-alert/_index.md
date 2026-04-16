---
title: Monitor YugabyteDB
headerTitle: Monitor YugabyteDB
linkTitle: Monitor
description: Overview of monitoring and alerts for YugabyteDB databases
headcontent: Monitor cluster performance and activity
menu:
  v2.25:
    identifier: monitor-and-alert
    parent: launch-and-manage
    weight: 60
type: indexpage
---

{{< page-finder/head text="Monitor YugabyteDB" subtle="across different products">}}
  {{< page-finder/list icon="/icons/database-hover.svg" text="YugabyteDB" current="" >}}
  {{< page-finder/list icon="/icons/server-hover.svg" text="YugabyteDB Anywhere" url="../../yugabyte-platform/alerts-monitoring/" >}}
  {{< page-finder/list icon="/icons/cloud-hover.svg" text="YugabyteDB Aeon" url="/preview/yugabyte-cloud/cloud-monitor/" >}}
{{< /page-finder/head >}}

{{<index/block>}}

  {{<index/item
    title="Metrics"
    body="Learn about selecting and using YugabyteDB metrics."
    href="metrics/"
    icon="fa-thin fa-chart-bar">}}

  {{<index/item
    title="Monitor xCluster"
    body="Monitor the state and health of xCluster replication."
    href="xcluster-monitor/"
    icon="fa-thin fa-clone">}}

  {{<index/item
    title="Active Session History"
    body="Leran about YSQL views, query identifiers, and wait events that are exposed via active sessions captured by ASH."
    href="active-session-history-monitor/"
    icon="fa-thin fa-monitor-waveform">}}

  {{<index/item
    title="Query tuning"
    body="Optimize query performance with tuning techniques and tools."
    href="query-tuning/"
    icon="fa-thin fa-gauge-high">}}

{{</index/block>}}
