---
title: Enable High Availability features
headerTitle: Enable high availability
linkTitle: High availability
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

One of Yugabyte’s biggest selling features is the high availability of DB clusters (universes) due to YB’s distributed architecture. This has been something that has been missing on the platform side of things. The platform is the central source of DB orchestration, monitoring, alerting, and so on. This means that when the platform is down/unavailable, customers lose the ability to manage + monitor their clusters (including stuff like metric collection). In an even more severe scenario, if a VM hosting the platform permanently crashes/it’s disk is wiped away, customers lose the ability to monitor/manage their universes and have lost their configurations/metrics data for good.

Platform HA tries to address this shortcoming by implementing an active-standby model for multiple platforms in a cluster with asynchronous replication. This means that a customer’s YB platform data can be replicated across multiple VMs ensuring that one VM crashing will not result in the customer not being able to manage/monitor their universes.

## General Architecture

Each HA cluster includes a single **active platform** and at least one **standby platform**, configured as follows:

* The active platform runs normally, but also pushes out backups of it’s state to all of the standby platforms in the HA cluster.

* Standby platforms are completely passive while in standby mode and cannot be used for managing/monitoring clusters until they are manually promoted to active by a user.

**Backups** from the active platform are periodically taken and pushed to followers at a customer-configured frequency (no more than once every minute). The active platform will also create + send one-off backups to standbys whenever a task completes (for example, creating a new universe). Metrics will be duplicated to standby platforms using Prometheus federation. Standby platforms will persist the 10 most recent backups to disk.

When **promoting a standby platform** to active, Yugabyte Platform restores the user-selected backup and automatically demotes the previous active platform to standby mode.

## Setting up an HA Cluster

### Prerequisites

* Yugabyte Platform v2.5.3.1 or above.
* You have already installed multiple YB Platforms to be used in the HA cluster. They should be [installed]() the same way they normally are installed.
* Platform VMs are all able to connect to each other over the port that the Platform UI/API is normally reachable over (80/443, for example).
* All platforms are running the same version of Yugabyte Platform software (it is good practice to upgrade all platforms in the HA cluster at close to the same time).

### Set Up the Active Platform

1. Go to the "Administration" tab on the bottom of the left-hand navigation panel.

1. You should be presented with a screen similar to:

    ![Add Node Actions](/images/ee/node-actions-add-node.png)

1. Ensure "Active" instance type is selected

1. Enter this platform’s IP/hostname (ensure to include the HTTP/HTTPS protocol prefix)

1. Generate a key

1. Select your desired replication frequency

1. Click "Create"

1. Now navigate to the "Instance Configuration" tab at the top of the screen

1. You should now see the address for this platform as the only entry row in the table like:

    ![Add Node Actions](/images/ee/node-actions-add-node.png)

1. Now click "Add Instance" for each standby platform you would like to add to the HA cluster and enter the standby platform’s IP address/hostname.

### Set up a Standby Platform

1. Go to the same "Administration" tab

1. Select "Standby" as the Instance type

1. Enter this platform’s IP/hostname (ensure to include the HTTP/HTTPS protocol prefix)

1. Copy the shared authentication key that was generated on the active platform + paste it into the shared authentication key text box (make sure it matches!)

1. Wait for however long you had set up the replication frequency on the active platform and then refresh the page. You should see that the other instances in the HA cluster appear in the list of instances here

1. Repeat all of these steps for each standby platform you want to add to the HA cluster

### Promote a Standby Platform to Active

1. Go to the standby platform you desire to promote to active

1. Select the "Make Active" button in the upper right-hand corner of the "Replication Configuration" tab

1. Select the backup you want to restore (note: it is strongly recommended to choose the most recent * backup in the vast majority of cases)

1. Confirm that you want to continue with the promotion

1. You will be logged out when the restore completes (this should only take a few seconds). When logging back in, please note that you will have to use the user-credentials that you had configured on the previously active platform

1. Once logged in, you should see that all of the data has been restored into the platform including universes, users, metrics, alerts, task history, cloud providers, and so on.
