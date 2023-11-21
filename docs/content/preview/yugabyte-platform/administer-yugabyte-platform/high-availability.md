---
title: High availability of YugabyteDB Anywhere
headerTitle: Enable high availability
linkTitle: Enable high availability
description: Make YugabyteDB Anywhere highly available
aliases:
  - /preview/yugabyte-platform/manage-deployments/platform-high-availability/
  - /preview/yugabyte-platform/manage-deployments/high-availability/
menu:
  preview_yugabyte-platform:
    identifier: platform-high-availability
    parent: administer-yugabyte-platform
    weight: 40
type: docs
---

YugabyteDB Anywhere high availability (HA) is an active-standby model for multiple YugabyteDB Anywhere instances. YugabyteDB Anywhere HA uses YugabyteDB's distributed architecture to replicate your YugabyteDB Anywhere data across multiple virtual machines (VM), ensuring that you can recover quickly from a VM failure and continue to manage and monitor your universes, with your configuration and metrics data intact.

Each HA cluster includes a single active YugabyteDB Anywhere instance and at least one standby YugabyteDB Anywhere instance, configured as follows:

- The active instance runs normally, but also pushes out backups of its state to all of the standby instances in the HA cluster.
- A standby instance is completely passive while in standby mode and cannot be used for managing or monitoring clusters until you manually promote it to active.

Backups from the active instance are periodically taken and pushed to standby instances at a configurable frequency (no more than once per minute). The active instance also creates and sends one-off backups to standby instances whenever a task completes (such as creating a new universe). Metrics are duplicated to standby instances using Prometheus federation. Standby instances retain the ten most recent backups on disk.

When you promote a standby instance to active, YugabyteDB Anywhere restores your selected backup, and then automatically demotes the previous active instance to standby mode.

## Prerequisites

Before configuring a HA cluster for your YugabyteDB Anywhere instances, ensure that you have the following:

- [Multiple YugabyteDB Anywhere instances](../../install-yugabyte-platform/) to be used in the HA cluster.
- YugabyteDB Anywhere VMs can connect to each other over the port where the YugabyteDB Anywhere UI is typically reachable (port 80 and 443, for example).
- All YugabyteDB Anywhere instances are running the same version of YugabyteDB Anywhere software. You should upgrade all YugabyteDB Anywhere instances in the HA cluster at approximately the same time.

## Configure active and standby instances

To set up HA for YugabyteDB Anywhere, you first configure the active instance by creating an active replication configuration and generating a shared authentication key.

You then configure one or more standby instances by creating standby replication configurations, using the shared authentication key generated on the active instance.

If your instances are using the HTTPS protocol, you must also add the root certificates for the active and standby instances to the [YugabyteDB Anywhere trust store](../../security/enable-encryption-in-transit/#add-certificates-to-your-trust-store) on the active instance.

### Configure the active instance

You can configure the active instance as follows:

1. Navigate to **Admin** and make sure that **High Availability > Replication Configuration > Active** is selected, as per the following illustration:

    ![Replication configuration tab](/images/yp/high-availability/replication-configuration.png)

1. Enter the instance IP address or hostname, including the HTTP or HTTPS protocol prefix and port if you are not using the default of 80 or 443.

1. Click **Generate Key** and copy the shared key.

1. Select your desired replication frequency, in minutes.

    In most cases, you do not need to replicate very often. A replication interval of 5-10 minutes is recommended. For testing purposes, a 1-minute interval is more convenient.

1. Click **Create**.

1. Switch to **Instance Configuration**.

    The address for this active instance should be the only information under **Instances**.

1. If the active instance is using the HTTPS protocol and self-signed certificates (that is, not signed by a trusted Certificate Authority (CA)), you need to get the root CA certificate that was used to sign the client certificate for the active instance.

    If you installed YBA using YBA Installer, copy the certificate from `/opt/yugabyte/data/yba-installer/certs`.

1. Add the active instance root certificate to the [YugabyteDB Anywhere trust store](../../security/enable-encryption-in-transit/#add-certificates-to-your-trust-store) of the active instance.

    This allows a standby to connect to the active instance if the standby is promoted to active status.

Your active instance is now configured.

### Configure standby instances

After the active instance has been configured, you can configure one or more standby instances by repeating the following steps for each standby instance you wish to add to the HA cluster:

1. On the standby instance, navigate to **Admin > High Availability > Replication Configuration** and select **Standby**, as per the following illustration:

    ![Standby instance type](/images/yp/high-availability/standby-configuration.png)

1. Enter the instance's IP address or hostname, including the HTTP or HTTPS protocol prefix and port if you are not using the default of 80 or 443.

1. Paste the shared authentication key from the active instance into the **Shared Authentication Key** field.

1. Click **Create**.

1. If the standby instance is using the HTTPS protocol and self-signed certificates (that is, not signed by a trusted Certificate Authority (CA)), you need to get the root CA certificate that was used to sign the client certificate for the standby instance.

    If you installed YBA using YBA Installer, copy the certificate from `/opt/yugabyte/data/yba-installer/certs`.

1. Switch to the active instance.

1. Add the standby instance root certificate to the [YugabyteDB Anywhere trust store](../../security/enable-encryption-in-transit/#add-certificates-to-your-trust-store) on the active instance.

    This allows a standby to connect to the active instance if the standby is promoted to active status.

1. Navigate to **Admin > High Availability > Replication Configuration** and select **Instance Configuration**.

1. Click **Add Instance**, enter the new standby instance's IP address or hostname, including the HTTP or HTTPS protocol prefix and port if you are not using the default of 80 or 443.

1. Click **Continue** on the **Add Standby Instance** dialog.

1. Switch back to the new standby instance, wait for a replication interval to pass, and then refresh the page. The other instances in the HA cluster should now appear in the list of instances.

Your standby instance is now configured.

## Promote a standby instance to active

You can make a standby instance active as follows:

1. Open **Replication Configuration** of the standby instance that you wish to promote to active and click **Make Active**.

1. Use the **Make Active** dialog to select the backup from which you want to restore (in most cases, you should choose the most recent backup) and enable **Confirm promotion**.

1. Click **Continue**. The restore takes a few seconds, after which expect to be logged out.

1. Log in using credentials that you had configured on the previously active instance.

You should be able to see that all of the data has been restored into the instance, including universes, users, metrics, alerts, task history, cloud providers, and so on.

## Check results

During a HA backup, the entire YugabyteDB Anywhere state is copied. If your universes are visible through YugabyteDB Anywhere UI and the replication timestamps are increasing, the backup is successful.

## Upgrade instances

All instances involved in HA should be of the same YugabyteDB Anywhere version. If the versions are different, an attempt to promote a standby instance using a YugabyteDB Anywhere backup from an active instance may result in errors.

Even though you can perform an upgrade of all YugabyteDB Anywhere instances simultaneously and there are no explicit ordering requirements regarding upgrades of active and standby instances, it is recommended to follow these general guidelines:

- Start an upgrade with an active instance.
- After the active instance has been upgraded, ensure that YugabyteDB Anywhere is reachable by logging in and checking various pages.
- Proceed with upgrading standby instances.

The following is the detailed upgrade procedure:

1. Stop the HA synchronization. This ensures that only backups of the original YugabyteDB Anywhere version are synchronized to the standby instance.
1. Upgrade the active instance. Expect a momentary lapse in availability for the duration of the upgrade. If the upgrade is successful, proceed to step 3. If the upgrade fails, perform the following:

    - Decommission the faulty active instance in the active-standby pair.
    - Promote the standby instance.
    - Do not attempt to upgrade until the root cause of the upgrade failure is determined.
    - Delete the HA configuration and bring up another standby instance at the original YugabyteDB Anywhere version and reconfigure HA.
    - After the root cause of failure has been established, repeat the upgrade process starting from step 1. Depending on the cause of failure and its solution, this may involve a different YugabyteDB Anywhere version to which to upgrade.

1. On the upgraded instance, perform post-upgrade validation tests that may include creating or editing a universe, backups, and so on.
1. Upgrade the standby instance.
1. Enable HA synchronization.
1. Optionally, promote the standby instance with the latest backup synchronized from the YugabyteDB Anywhere version to which to upgrade.

The following diagram provides a graphical representation of the upgrade procedure:

![High availability upgrade](/images/yp/high-availability/ha-upgrade.png)

The following table provides the terminology mapping between the upgrade diagram and the upgrade procedure description:

| Diagram   | Procedure description                                        |
| --------- | ------------------------------------------------------------ |
| HA        | High availability                                            |
| Version A | Original YugabyteDB Anywhere version that is subject to upgrade |
| Version B | Newer YugabyteDB Anywhere version to which to upgrade        |

## Remove a standby instance

To remove a standby instance from a HA cluster, you need to remove it from the active instance's list, and then delete the configuration from the instance to be removed, as follows:

1. On the active instance's list, click **Delete Instance** for the standby instance to be removed.

1. On the standby instance you wish to remove from the HA cluster, click **Delete Configuration** on the **Admin** tab.

The standby instance is now a standalone instance again.

After you have returned a standby instance to standalone mode, the information on the instance is likely to be out of date, which can lead to incorrect behavior. It is recommended to wipe out the state information before using it in standalone mode. For assistance with resetting the state of a standby instance that you removed from a HA cluster, contact Yugabyte Support.

## Limitations

If you are using custom ports for Prometheus in your YugabyteDB Anywhere installation and the YugabyteDB Anywhere instance is configured for HA with other YugabyteDB Anywhere instances, then the following limitation applies:

- All YugabyteDB Anywhere instances configured under HA must use the same custom port.

    The default Prometheus port for YugabyteDB Anywhere is `9090`. Custom ports are configured through the settings section of the Replicated installer UI that is typically available at `https://<yugabyteanywhere-ip>:8800/`.

    For information on how to access the Replicated settings page, see [Install YugabyteDB Anywhere](../../install-yugabyte-platform/install-software/default/).
