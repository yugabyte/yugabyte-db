---
title: Enable High Availability features
headerTitle: Enable high availability
linkTitle: Enable high availability
description: Enable Yugabyte Platform's high-availability features
aliases:
menu:
  latest:
    identifier: platform-high-availability
    parent: manage-deployments
    weight: 41
isTocNested: true
showAsideToc: true
---

Yugabyte’s distributed architecture enables your database clusters (called universes) to have extremely high availability. And as the central source of database orchestration, monitoring, alerting, and more, Yugabyte Platform brings its own distributed architecture to the table in the form of the High Availability feature. 

Platform's high availability feature is an active-standby model for multiple platforms in a cluster with asynchronous replication. Your platform data is replicated across multiple VMs, ensuring that you can recover smoothly and quickly from a VM failure and continue to manage and monitor your universes, with your configuration and metrics data intact.

## General Architecture

Each HA cluster includes a single _active platform_ and at least one _standby platform_, configured as follows:

* **The active platform** runs normally, but also pushes out backups of it’s state to all of the standby platforms in the HA cluster.

* **A standby platform** is completely passive while in standby mode and can't be used for managing or monitoring clusters until you manually promote it to active.

**Backups** from the active platform are periodically taken and pushed to followers at a user-configurable frequency (no more than once per minute). The active platform also creates and sends one-off backups to standby platforms whenever a task completes (such as creating a new universe). Metrics are duplicated to standby platforms using Prometheus federation. Standby platforms retain the 10 most recent backups on disk.

When you promote a standby platform to active, Yugabyte Platform restores your selected backup, and automatically demotes the previous active platform to standby mode.

## Setting up an HA Cluster

### Prerequisites

* Yugabyte Platform v2.5.3.1 or above.
* You have already [installed](../../install-yugabyte-platform/) multiple YB Platform instances to be used in the HA cluster.
* Platform VMs are all able to connect to each other over the port that the Platform UI/API is normally reachable over (80/443, for example).
* All platforms are running the same version of Yugabyte Platform software (it is good practice to upgrade all platforms in the HA cluster at close to the same time).

### Set Up the Active Platform

1. Click the "Administration" tab at the bottom of the left-side navigation panel.

    <br/>

    ![PLACEHOLDER IMAGE](/images/placeholder-name.png)

1. Select the `Active` instance type.

    <br/>

    ![PLACEHOLDER IMAGE](/images/placeholder-name.png)

1. Enter this platform’s IP address or hostname (including the HTTP or HTTPS protocol prefix).

    <br/>

    ![PLACEHOLDER IMAGE](/images/placeholder-name.png)

1. Generate a key.

    <br/>

    ![PLACEHOLDER IMAGE](/images/placeholder-name.png)

1. Select your desired replication frequency, in minutes.

    <br/>

    In most cases, you don't need to replicate very often. Yugabyte recommends... **DANIEL, WHAT IS THE REC HERE?**

    <br/>

    ![PLACEHOLDER IMAGE](/images/placeholder-name.png)

1. Click "Create".

1. Next, click the "Instance Configuration" tab at the top of the screen.

    <br/>

    The address for this platform should be the only entry row in the table, similar to the following:

    <br/>

    ![PLACEHOLDER IMAGE](/images/placeholder-name.png)

1. Click "Add Instance" for each standby platform you would like to add to the HA cluster, and enter the standby platform’s IP address or hostname (including the HTTP/HTTPS protocol prefix).

    <br/>

    ![PLACEHOLDER IMAGE](/images/placeholder-name.png)

### Set up a Standby Platform

Once you've set up the active platform, you can set up one or more standby platforms.

**Repeat the following steps for each standby platform** you wish to add to the HA cluster:

1. Click the "Administration" tab at the bottom of the left-side navigation panel.

    <br/>

    You should be presented with a screen similar to the following:

    <br/>

    ![PLACEHOLDER IMAGE](/images/placeholder-name.png)

1. Select the `Standby` instance type.

    <br/>

    ![PLACEHOLDER IMAGE](/images/placeholder-name.png)

1. Enter this platform’s IP address or hostname (including the HTTP or HTTPS protocol prefix).

    <br/>

    ![PLACEHOLDER IMAGE](/images/placeholder-name.png)

1. Copy the shared authentication key you generated on the active platform, and paste it into the Shared authentication key field. Double-check to make sure the keys match.

    <br/>

    ![PLACEHOLDER IMAGE](/images/placeholder-name.png)

1. Wait for a replication interval (you set this when you [set up the active instance](#set-up-the-active-platform)), then refresh the page. The other instances in the HA cluster should now appear in the list of instances.

    <br/>

    ![PLACEHOLDER IMAGE](/images/placeholder-name.png)

### Promote a Standby Platform to Active

1. On the standby platform you wish to promote to active, click the Make Active button in the upper-right corner of the "Replication Configuration" tab.

    <br/>

    ![PLACEHOLDER IMAGE](/images/placeholder-name.png)

1. Select the backup you want to restore (we strongly recommend you choose the most recent backup in the vast majority of cases!), and confirm that you want to promote this instance to active.

    <br/>

    ![PLACEHOLDER IMAGE](/images/placeholder-name.png)

1. The restore should take only a few seconds, and you'll be logged out when it's finished. To log back in, use the user credentials that you had configured on the previously active platform.

1. Once you're logged in, you should see that all of the data has been restored into the platform including universes, users, metrics, alerts, task history, cloud providers, and so on.
