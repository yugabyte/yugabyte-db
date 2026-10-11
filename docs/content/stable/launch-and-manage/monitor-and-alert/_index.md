---
title: Monitor YugabyteDB
headerTitle: Monitor YugabyteDB
linkTitle: Monitor
description: Overview of monitoring and alerts for YugabyteDB databases
headcontent: Monitor cluster performance and activity
menu:
  stable:
    identifier: monitor-and-alert
    parent: launch-and-manage
    weight: 60
type: indexpage
---

{{< page-finder/head text="Monitor YugabyteDB" subtle="across different products">}}
  {{< page-finder/list icon="/icons/database-hover.svg" text="YugabyteDB" current="" >}}
  {{< page-finder/list icon="/icons/server-hover.svg" text="YugabyteDB Anywhere" url="../../yugabyte-platform/alerts-monitoring/" >}}
  {{< page-finder/list icon="/icons/cloud-hover.svg" text="YugabyteDB Aeon" url="/stable/yugabyte-cloud/cloud-monitor/" >}}
{{< /page-finder/head >}}

## Assess performance with an AI coding agent

If an AI coding agent (such as Claude Code, Cursor, or Codex) assesses universe performance for you, install the official [yb-performance-assessment skill](https://github.com/yugabyte/yugabytedb-skills/tree/main/skills/yb-performance-assessment) first. The skill runs cross-cutting checks, then hands off to the specialist skills, so the agent makes fewer errors.

To install the skill, run the following command:

```sh
npx skills add yugabyte/yugabytedb-skills -s yb-performance-assessment
```

The [yugabytedb-skills](https://github.com/yugabyte/yugabytedb-skills) repository also describes other ways to install the skill, including a Claude Skills upload and the plugin marketplace.

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
    body="Learn about YSQL views, query identifiers, and wait events that are exposed via active sessions captured by ASH."
    href="active-session-history-monitor/"
    icon="fa-thin fa-monitor-waveform">}}

  {{<index/item
    title="Tablet metadata"
    body="Look up local and cluster-wide tablet placement, leadership, and state."
    href="tablet-metadata/"
    icon="fa-thin fa-table-cells">}}

  {{<index/item
    title="Cluster-wide database views"
    body="Query per-node statistics views across every live YB-TServer from a single YSQL session."
    href="cluster-wide-db-views/"
    icon="fa-thin fa-layer-group">}}

  {{<index/item
    title="YSQL Distributed Tracing"
    body="Export OpenTelemetry traces for YSQL query execution and view them in Jaeger or other backends."
    href="ysql-distributed-tracing/"
    icon="fa-thin fa-chart-gantt">}}

  {{<index/item
    title="Query tuning"
    body="Optimize query performance with tuning techniques and tools."
    href="query-tuning/"
    icon="fa-thin fa-gauge-high">}}

{{</index/block>}}
