---
title: Audit account activity
linkTitle: Audit account activity
description: Monitor activity in YugabyteDB Managed.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  preview:
    identifier: cloud-activity
    parent: cloud-secure-clusters
    weight: 500
isTocNested: true
showAsideToc: true
---

Audit your account activity using the **Activity** tab on the **Admin** page, which lists the source, activity, user, and time of the activity.

Cluster activity is also displayed on the cluster [**Activity** tab](../../cloud-monitor/monitor-activity).

![Activity tab](/images/yb-cloud/cloud-admin-activity.png)

To view activity details and associated messages, click the right arrow in the list to display the **Activity Details** sheet.

To filter the activity list, enter a search term. You can also filter the list by Source, Activity, and Date range.

## Logged activity

The following table lists the activity that is logged.

| Source | Activity |
| --- | --- |
| Cluster | Create Cluster<br>Delete Cluster<br>Edit Cluster<br>Upgrade Cluster<br>Pause Cluster<br>Resume Cluster |
| Allow List | Create Allow List<br>Delete Allow List |
| Backup | Create Backup<br>Delete Backup<br>Restore Backup |
| Billing | Add Billing<br>Edit Billing |
| Payment | Add Payment<br>Edit Payment<br>Delete Payment |
| Users | Remove User<br>Update Role<br>Add User<!-- <br>Activate user -->|
| Backup Schedule | Add Backup Schedule<br>Edit Backup Schedule<br>Delete Backup Schedule |
