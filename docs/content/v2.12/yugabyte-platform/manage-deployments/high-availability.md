---
title: Enable High Availability features
headerTitle: Enable high availability
linkTitle: Enable high availability
description: Enable Yugabyte Platform's high-availability features
menu:
  v2.12_yugabyte-platform:
    identifier: platform-high-availability
    parent: manage-deployments
    weight: 40
type: docs
---

Yugabyte’s distributed architecture enables your database clusters (called universes) to have extremely high availability. And as the central source of database orchestration, monitoring, alerting, and more, Yugabyte Platform brings its own distributed architecture to the table in the form of the High Availability feature.

Platform's high availability feature is an active-standby model for multiple platforms in a cluster with xCluster replication. Your platform data is replicated across multiple VMs, ensuring that you can recover smoothly and quickly from a VM failure and continue to manage and monitor your universes, with your configuration and metrics data intact.

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

### Set up the Active Platform

1. Click the "Admin" tab at the bottom of the left-side navigation panel.

    <br/>

    ![Replication configuration tab](/images/yp/high-availability/replication-configuration.png)

1. Select the `Active` instance type.

1. Enter this platform’s IP address or hostname (including the HTTP or HTTPS protocol prefix, and port if you're not using the default of 80 or 443).

1. Click Generate Key, and copy the shared key.

1. Select your desired replication frequency, in minutes.

    <br/>

    In most cases, you don't need to replicate very often. A replication interval of 5-10 minutes is generally recommended. (For testing purposes, a 1-minute interval is more convenient; set to longer when you're done testing.)

1. Click "Create".

1. Next, click the "Instance Configuration" tab at the top of the screen.

    <br/>

    The address for this platform should be the only entry row in the table.

    <br/>

    ![Instance list](/images/yp/high-availability/instance-configuration-active.png)

**Your active instance is now configured**. Next, continue to [set up one or more standby platforms](#set-up-standby-platforms).

### Set up Standby Platforms

Once you've set up the active platform, you can set up one or more standby platforms.

**Repeat the following steps for each standby platform** you wish to add to the HA cluster:

1. Click the "Admin" tab at the bottom of the left-side navigation panel.

    <br/>

    You should be presented with a screen similar to the following:

    <br/>

    ![Replication configuration tab](/images/yp/high-availability/replication-configuration.png)

1. Select the `Standby` instance type.

    <br/>

    ![Standby instance type](/images/yp/high-availability/standby-configuration.png)

1. Enter this platform’s IP address or hostname (including the HTTP or HTTPS protocol prefix, and port if you're not using the default of 80 or 443).

1. Paste the shared authentication key from the active platform into the Shared authentication key field. Double-check to make sure the keys match.

    <br/>

    ![Shared key field](/images/yp/high-availability/shared-key-field.png)

1. **On the active platform**, click the Instance Configuration tab. Click Add Instance, enter the new standby's IP address or hostname (including the HTTP or HTTPS protocol prefix, and port if you're not using the default of 80 or 443), and click Continue.

    <br/>

    ![Add an instance](/images/yp/high-availability/add-standby-instance.png)

1. **On the new standby platform**, wait for a replication interval (you set this when you [set up the active instance](#set-up-the-active-platform)), then refresh the page. The other instances in the HA cluster should now appear in the list of instances.

    <br/>

    ![Instance list](/images/yp/high-availability/instance-configuration.png)

    If you click on other tabs, notice that they are greyed out. You can only use those tabs on the active instance.

**Your standby instances are now set up**. The next section details how to [promote a standby platform instance to active](#promote-a-standby-platform-to-active), and the final section explains how to [delete a standby instance](#remove-a-standby-platform).

### Promote a Standby Platform to Active

1. On the standby platform you wish to promote to active, click the Make Active button in the upper-right corner of the "Replication Configuration" tab.

    <br/>

    ![Standby instance replication configuration](/images/yp/high-availability/standby-replication-configuration.png)

1. Select the backup you want to restore (we strongly recommend you **choose the most recent backup** in the vast majority of cases!), and ensure the Confirm promotion box is checked.

    <br/>

    ![Backup selection](/images/yp/high-availability/restore-backup.png)
    <br/>

    The restore should take only a few seconds, and you'll be logged out when it's finished.

1. To log back in, use the user credentials that you had configured on the previously active platform.

    <br/>

    Once you're logged in, you should see that all of the data has been restored into the platform including universes, users, metrics, alerts, task history, cloud providers, and so on.

### Remove a Standby Platform

To remove a standby platform from a high-availability cluster, you remove it from the active platform's list, and then delete the configuration from the platform to be removed.

1. **On the active platform**'s instance list, click the Delete Instance button for the standby instance to be removed, and confirm the deletion.

    <br/>

    ![Delete instance](/images/yp/high-availability/delete-instance.png)

1. **On the standby platform** you wish to remove from the high-availability cluster, click the Delete Configuration button on the Admin tab, and confirm the deletion.

    <br/>

    ![Delete configuration](/images/yp/high-availability/delete-configuration.png)

The standby platform is now a standalone instance again.

{{< tip title="Reset the Platform state" >}}

After you've returned a standby instance to standalone mode, the information on the instance is likely to be out of date, which can lead to incorrect behavior. Yugabyte strongly recommends wiping out the state information before using it in standalone mode. [Contact Yugabyte support](https://support.yugabyte.com/) for help resetting the state of a standby platform that you've removed from a high-availability cluster.

**Note**: The high-availability feature is in active development, and this requirement will be removed in a future version.

{{</ tip >}}
