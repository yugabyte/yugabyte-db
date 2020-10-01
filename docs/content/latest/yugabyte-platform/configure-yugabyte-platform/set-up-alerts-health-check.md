---
title: Set up alerts and health checking
headerTitle: Set up the alerts and health checking
linkTitle: Alerts and health checking
description: Set up the initial alerts and health checking.
menu:
  latest:
    identifier: set-up-alerts-health-checking
    parent: configure-yugabyte-platform
    weight: 40
isTocNested: true
showAsideToc: true
---

To help you stay aware of potential deployment issues, the Yugabyte Platform has the capability to check on each individual universe for several types of issues and proactively send out email alerts when problems arise.

## Set up alerts and health checking

To configure health checking, go to your profile page in the Yugabyte Platform console by clicking the top-right drop-down list featuring your account email and then select the **Profile** option.

![Profile Dropdown](/images/ee/health/profile-button.png)

You should see something like the following:

![Alerting Controls](/images/ee/health/alerting-controls.png)

Under the **Alerting Controls**, you can::

- Add email addresses to send alerts to. Use comma-separated values.
- Enable or disable sending the same alerts back to the Yugabyte support team.

Setting at least one email address or enabling sending to Yugabyte will enable the feature and subsequently begin to track the health of your universes. There are two modes of operation:

- Every 5 minutes, the background checker goes to every universe and performs a set of checks on each node. If any checks fail, the **Health** tab of the universe highlights the errors and sends an alert message by email to all of the email addresses.
- Every 12 hours, whether or not there are errors, a status email message is sent out to ensure that health checking is occurring.

Both timing options are hard-coded, but will become user-configurable knobs.

## How to view the health of a universe

To see the health of your universe, navigate to any of your universes and click the **Health** tab. Here is an example:

![Universe Health](/images/ee/health/universe-health.png)

As you can see, the checks run every five minutes, across every node. The following are explicitly checked:

- Uptime of both the `yb-master` and `yb-tserver` processes, which could indicate a node or process restart.
- Disk utilization on the various partitions configured on your nodes to ensure the database does not run out of storage.
- Presence of any internal google logging `FATAL` files, indicating some previous serious failure of Yugabyte.
- Presence of any core files, indicating some previous serious failure of Yugabyte.
- Total number of open file descriptors, which if too great, might end up causing problems in normal operation.
- Connectivity with either `ycqlsh` or `redis-cli`, which could indicate either network connectivity issues in your deployment or server-side issues processing requests.

This list is not exhaustive — Yugabyte is actively working on expanding the metrics. Also, Yugabyte is working on more tightly integrating with the underlying Prometheus server that is bundled with Yugabyte Platform to provide more granular, user-configurable, metrics-based alerting.
