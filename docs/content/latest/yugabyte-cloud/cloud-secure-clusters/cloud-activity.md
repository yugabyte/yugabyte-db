---
title: Audit cloud activity
linkTitle: Audit cloud activity
description: Monitor activity on your Yugabyte Cloud.
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: cloud-activity
    parent: cloud-secure-clusters
    weight: 500
isTocNested: true
showAsideToc: true
---

Audit your cloud activity using the **Activity** tab on the **Admin** page, which lists the source, activity, user, and time of the activity.

![Cloud Activity tab](/images/yb-cloud/cloud-admin-activity.png)

To view activity details and associated messages, click the right arrow in the list to display the **Activity Details** sheet.

To filter the activity list, enter a search term. You can also filter the list by Source, Activity, and date range.

## Logged activity

The following table lists the cloud activity that is logged.

| Source | Activity |
| --- | --- |
| Cluster | Create cluster<br>Delete cluster<br>Edit cluster |
| Allow list | Create allow list<br>Delete allow list |
| Backup | Create backup<br>Delete backup<br>Restore backup |
| Billing | Add billing profile<br>Update billing profile<br>Add credit card<br>Set default credit card |
| Users | Remove user<br>Update role<br>Add user<!-- <br>Activate user -->|
| Backup schedule | Add backup schedule<br>Update backup schedule<br>Delete backup schedule |
