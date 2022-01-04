---
title: Cloud alerts
linkTitle: Alerts
description: Set alerts for activity in your cloud.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: cloud-alerts
    parent: cloud-monitor
    weight: 50
isTocNested: true
showAsideToc: true
---

Use alerts to notify you and your team members when cluster and database resource use exceeds predefined limits, or of potential billing issues.

## Configure alerts

Enable alerts for your cloud using the **Configuration** tab on the **Alerts** page.

If alerts are enabled, Yugabyte Cloud will send email notifications to all cloud account members.

![Cloud Alerts Configurations tab](/images/yb-cloud/cloud-alerts-configurations.png)

To view alert details, select the alert to display the **Alert Policy Settings** sheet.

To change an alert, click the toggle in the **Status** column, or on the **Alert Policy Settings** sheet.

To test that you can receive email from the Yugabyte server, click **Send Test Email**.

### Cluster alerts

The following table lists the alerts that are available for clusters.

| Cluster activity | Criteria | Severity |
| --- | --- | --- |
| Memory use | Exceeds 75% for at least 10 minutes | Warning |
| Memory use | Exceeds 90% for at least 10 minutes | Severe |
| CPU use | Exceeds 70% for at least 30 minutes | Warning |
| CPU use | Exceeds 90% for at least 30 minutes | Severe |
| Storage | Below 40% | Warning |
| Storage | Below 25% | Severe |

### Database alerts

The following table lists the alerts that are available for databases.

| Database activity | Criteria | Severity |
| --- | --- | --- |
| Overload | | Severe |
| High number of YSQL connections | | Severe |

### Billing alerts

The following table lists the alerts that are available for billing issues.

| Alert | Notes | Severity |
| --- | --- | --- |
| Credit card expiring | Triggered one month before the card expiry date | Severe |
| Payment failed | | Severe |
| Credit usage exceeds the 50% limit | | Warning |
| Credit usage exceeds the 75% limit | | Warning |
| Credit usage exceeds the 90% limit | | Severe |
