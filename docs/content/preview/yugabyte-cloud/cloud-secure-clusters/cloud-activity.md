---
title: Audit account activity
linkTitle: Audit account activity
description: Monitor activity in YugabyteDB Aeon.
headcontent: Monitor account and cluster activity in YugabyteDB Aeon
menu:
  preview_yugabyte-cloud:
    identifier: cloud-activity
    parent: cloud-secure-clusters
    weight: 500
type: docs
---

Audit your account activity using the **Activity** tab on the **Security** page. The **Activity** tab provides the following two logs:

- **Console Audit Log** - lists cluster and account activity, including the activity type, who performed the activity, timestamp, and result.
- **Access History** - lists all login activity, including the client IP address, activity type, number of attempts, timestamp, and result.

Cluster activity is also displayed on the cluster [**Activity** tab](../../cloud-monitor/monitor-activity).

![Activity tab](/images/yb-cloud/cloud-admin-activity.png)

To view activity details and associated messages, select the activity in the list to display the **Activity Details** sheet.

To filter the activity list, enter a search term. You can also filter the list by Source, Activity, Performed by, Result, and Date range.

<!--
## Logged activity

The following table lists the activity that is logged.

| Source | Activity |
| :----- | :------- |
| Allow List | Create Allow List<br>Delete Allow List |
| API Key | Create API Key<br>Expire API Key<br>Revoke API Key |
| Backup | Create Backup<br>Delete Backup<br>Restore Backup |
| Backup Schedule | Add Backup Schedule<br>Edit Backup Schedule<br>Delete Backup Schedule |
| Billing | Create Billing<br>Edit Billing |
| Cluster | Create Cluster<br>Delete Cluster<br>Edit Cluster<br>Upgrade Cluster<br>Pause Cluster<br>Resume Cluster |
| Cluster Metrics Exporter | Configure Cluster Metrics Exporter<br>Stop Cluster Metrics Exporter<br>Start Cluster Metrics Exporter<br>Remove Cluster Metrics Exporter |
| CMK | Create CMK<br>Edit CMK<br>Enable CMK<br>Disable CMK<br>Rotate CMK Configuration |
| Free Trial | Request Free Trial<br>Approve Free Trial<br>Reject Free Trial |
| Login Types | Edit Login Types |
| Maintenance | Edit Maintenance Window<br>Edit Maintenance Exclusion<br>Schedule Maintenance Event |
| Export Configuration | Create export configuration<br>Edit export configuration<br>Delete export configuration |
| Payment | Add Payment<br>Edit Payment<br>Delete Payment |
| Read Replica | Create read replica<br>Edit read replica<br>Delete read replica |
| User | Add User<br>Edit User<br>Remove User<br>Activate user |
| VPC | Create VPC<br>Delete VPC |
| VPC Peering | Create VPC Peering<br>Delete VPC Peering |
-->
