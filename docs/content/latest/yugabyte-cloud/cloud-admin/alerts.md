<!--
title: Manage Cloud Alerts
linkTitle: Alerts
description: Configure Yugabyte Cloud alerts.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: cloud-alerts
    parent: cloud-admin
    weight: 200
isTocNested: true
showAsideToc: true
-->

The **Alert Configurations** tab displays a list of alerts configured for your cloud that includes username, cluster, authenication method, and YugabyteDB roles.

![Admin Alert Configuration page](/images/yb-cloud/cloud-admin-alerts.png)

Use the **Alert Configurations** tab to manage alerts for your cloud. You can configure the following types of alerts:

* Cluster
    \
    Configure alerts for all your clusters or selected clusters, based on a variety of metrics such a CPU use and disk space.

* Billing
    \
    Configure alerts for billing-related metrics. 

You can also set the recipients for alerts, and the type of notification to send - system message, email, Slack, or PagerDuty.

To view and respond to your alerts, refer to [Manage Notifications](../../cloud-console/notifications/).

## Create a cluster alert

To add a cluster alert:

1. On the **Alert Configurations** tab, click **Create Alert** and choose **Cluster Alert** to display the **Create Cluster Alert** sheet.
1. Enter a name and description for the alert.
1. Choose the target.
1. Set the conditions for the alert:

    * **Metric** - choose the criteria to use to trigger the alert.
    * **Severity** - define the criteria for the alert. The conditions for triggering an alert will vary depending on the selected Metric.

1. Add recipients for the alert. You can select roles or individual users.
1. Select the types of notifications to send.

### Turn alerts for a cluster on and off

To turn alerts for a specific cluster on and off:

1. On the **Clusters** page, select the cluster, then click the **Settings** tab.
1. Under **General**, set the Alerts **Enabled** option to on or off.

## Create a billing alert

To add a billing alert:

1. On the **Alert Configurations** tab, click **Create Alert** and choose **Billing Alert** to display the **Create Billing Alert** sheet.
1. Enter a name and description for the alert.
1. Choose the target.
1. Set the conditions for the alert:

    * **Metric** - choose the criteria to use to trigger the alert.
    * **Severity** - define the criteria for the alert. The conditions for triggering an alert will vary depending on the selected Metric.

1. Add recipients for the alert. You can select roles or individual users.
1. Select the types of notifications to send.
