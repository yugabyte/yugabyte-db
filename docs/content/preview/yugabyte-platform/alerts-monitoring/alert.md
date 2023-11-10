---
title: Alerts in YugabyteDB Anywhere
headerTitle: Alerts
linkTitle: Alerts
description: Use alerts
headcontent: Set alerts for activity in your account
menu:
  preview_yugabyte-platform:
    identifier: alert
    parent: alerts-monitoring
    weight: 10
type: docs
---

YugabyteDB Anywhere allows you to view a list of generated alerts and manage these alerts by navigating to **Alerts**, as per the following illustration:

![Alerts Page](/images/yp/alerts-monitoring/alerts-view1.png)

By default, the list is sorted in reverse chronological order by the alert issue time. You can reorder the list by clicking column headers.

{{< tip title="Configure alerts" >}}

For information on configuring your alerts, both for universe and YugabyteDB Anywhere metrics, refer to [Create and configure alerts](../../configure-yugabyte-platform/set-up-alerts-health-check/).

{{< /tip >}}

You can access detailed information about a specific alert by clicking on its name to open the **Alert Details** dialog shown in the following illustration:

![Alert Details](/images/yp/alerts-monitoring/alerts-view2.png)

The alert status and timeframe provide information on when the threshold was exceeded during the most recent alert check. For example, suppose there is an alert rule that checks if the average CPU utilization is higher than 75% for 15 minutes. In this case, the alert is triggered for as long as the CPU usage is higher than 75%. To avoid receiving continuous alerts, you can either acknowledge the active alert therefore preventing YugabyteDB Anywhere from sending additional notifications to the alert's destination, or you can resolve the alert by addressing the condition that triggered it.

To summarize, the alert status can be active, acknowledged, or resolved. You change the status by taking an appropriate action, such as, for example, **Acknowledge** for an active alert. Note that if you are using a read-only account for YugabyteDB Anywhere, you cannot perform actions.

For additional information, see the following:

- [Create and configure alerts](../../configure-yugabyte-platform/set-up-alerts-health-check/)
- [Alerts and notifications in YugabyteDB Anywhere](https://www.yugabyte.com/blog/yugabytedb-2-8-alerts-and-notifications/)
- [Metrics in YugabyteDB Anywhere](../anywhere-metrics/)
