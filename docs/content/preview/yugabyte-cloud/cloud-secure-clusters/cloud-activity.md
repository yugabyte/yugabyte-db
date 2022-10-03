---
title: Audit account activity
linkTitle: Audit account activity
description: Monitor activity in YugabyteDB Managed.
headcontent: Monitor account and cluster activity in YugabyteDB Managed
image: /images/section_icons/deploy/enterprise.png
menu:
  preview_yugabyte-cloud:
    identifier: cloud-activity
    parent: cloud-secure-clusters
    weight: 500
type: docs
---

Audit your account activity using the **Activity** tab on the **Admin** page, which lists the source, activity, user, and time of the activity.

Cluster activity is also displayed on the cluster [**Activity** tab](../../cloud-monitor/monitor-activity).

You must be signed in as an Admin user; Developer users cannot view the account activity.

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
| Maintenance | Edit Maintenance Window<br>Edit Maintenance Exclusion<br>Schedule Maintenance Event |
| Billing | Add Billing<br>Edit Billing |
| Payment | Add Payment<br>Edit Payment<br>Delete Payment |
| Account | Add User<br>Edit User<br>Remove User<!-- <br>Activate user -->|
| Backup Schedule | Add Backup Schedule<br>Edit Backup Schedule<br>Delete Backup Schedule |
| VPC | Create VPC<br>Delete VPC |
| VPC Peering | Create VPC Peering<br>Delete VPC Peering |
| API Key | Create API Key<br>Expire API Key<br>Revoke API Key |
