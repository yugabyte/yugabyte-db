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

| Cluster activity | Criteria | Solution |
| --- | --- | --- |
| Memory use | Exceeds 75% for at least 10 minutes (Warning)<br>Exceeds 90% for at least 10 minutes (Severe) | Add vCPUs. |
| CPU use | Exceeds 70% for at least 30 minutes (Warning)<br> Exceeds 90% for at least 30 minutes (Severe) | Add vCPUs. |
| Storage | Below 40% (Warning)<br>Below 25% (Severe) | Increase disk size per vCPU or add vCPUs. |

Once you have 16 vCPUs per node, consider adding nodes. For information on scaling clusters, refer to [Scale and configure clusters](../../cloud-clusters/configure-clusters/).

Monitor these metrics on the cluster [Overview](../overview/#overview-metrics) or [Performance metrics](../overview/#performance-metrics) tab.

### Database alerts

The following table lists the alerts that are available for databases.

| Database activity | Criteria | Solution |
| --- | --- | --- |
| Overload | | |
| High number of YSQL connections | | |

### Billing alerts

The following table lists the alerts that are available for billing issues.

| Alert | Notes | Solution |
| --- | --- | --- |
| Credit card expiring | Triggered one month before the card expiry date | Add a new credit card. |
| Payment failed | | Add a new credit card or contact Support. |
| Credit usage exceeds the 50% limit | | Add a new credit card or contact Support. |
| Credit usage exceeds the 75% limit | | Add a new credit card or contact Support. |
| Credit usage exceeds the 90% limit | | Add a new credit card or contact Support. |

For information on adding credit cards and managing your billing profile, refer to [Manage credit cards](../../cloud-admin/cloud-billing-profile/#manage-credit-cards).
