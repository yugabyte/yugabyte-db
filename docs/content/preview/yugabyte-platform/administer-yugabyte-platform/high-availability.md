---
title: High availability of YugabyteDB Anywhere
headerTitle: Enable high availability
description: Make YugabyteDB Anywhere highly available
headcontent: Configure standby instances of YugabyteDB Anywhere
linkTitle: Enable high availability
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

YugabyteDB Anywhere (YBA) high availability (HA) is an active-standby model for multiple YBA instances. YBA HA uses YugabyteDB's distributed architecture to replicate your YBA data across multiple virtual machines (VM), ensuring that you can recover quickly from a VM failure and continue to manage and monitor your universes, with your configuration and metrics data intact.

Each HA cluster includes a single active YBA instance and at least one standby YBA instance, configured as follows:

- The active instance runs normally, but also pushes out backups of its state to all of the standby instances in the HA cluster.
- A standby instance is completely passive while in standby mode and cannot be used for managing or monitoring clusters until you manually promote it to active.

Backups from the active instance are periodically taken and pushed to standby instances at a configurable frequency (no more than once per minute). The active instance also creates and sends one-off backups to standby instances whenever a task completes (such as creating a new universe). Metrics are duplicated to standby instances using Prometheus federation. Standby instances retain the ten most recent backups on disk.

When you promote a standby instance to active, YBA restores your selected backup, and then automatically demotes the previous active instance to standby mode.

## Prerequisites

Before configuring a HA cluster for your YBA instances, ensure that you have the following:

- [Multiple YBA instances](../../install-yugabyte-platform/) to be used in the HA cluster.
- The YBA instances can connect to each other over the port where the YugabyteDB Anywhere UI is reachable (typically 443).
- Communication is open in both directions over ports 9000 and 9090 on all YBA instances.
- If you are using custom ports for Prometheus, all YBA instances are using the same custom port. The default Prometheus port for YugabyteDB Anywhere is 9090.
- All YBA instances are running the same version of YBA software. You should upgrade all YBA instances in the HA cluster at approximately the same time.

## Configure active and standby instances

To set up HA, you first configure the active instance by creating an active HA replication configuration and generating a shared authentication key.

You then configure one or more standby instances by creating standby HA replication configurations, using the shared authentication key generated on the active instance.

If your instances are using the HTTPS protocol (the default), you must also add the root certificates for the active and standby instances to the [YugabyteDB Anywhere trust store](../../security/enable-encryption-in-transit/#add-certificates-to-your-trust-store) on the active instance.

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

1. If the active instance is using the HTTPS protocol and was set up to use self-signed certificates instead of a trusted Certificate Authority (CA), you will need to obtain the CA certificate for this YBA instance.

    - If you installed YBA using [YBA Installer](../../install-yugabyte-platform/install-software/installer/), locate the CA certificate from the path `/opt/yugabyte/data/yba-installer/certs/ca_cert.pem` on the YBA instance. You may need to replace `/opt/yugabyte` with the path to a custom install root if you configured yba installer using the [configuration options](../../install-yugabyte-platform/install-software/installer/#configuration-options).

    - If you installed YBA using [Replicated](../../install-yugabyte-platform/install-software/replicated/), locate the CA certificate from the path `/var/lib/replicated/secrets/ca.crt` on the YBA instance.

1. Add the active instance CA certificate to the [YugabyteDB Anywhere trust store](../../security/enable-encryption-in-transit/#add-certificates-to-your-trust-store) of the active instance.

    This allows a standby to connect to the active instance if the standby is promoted to active status.

Your active instance is now configured.

### Configure standby instances

After the active instance has been configured, you can configure one or more standby instances by repeating the following steps for each standby instance you wish to add to the HA cluster:

1. On the standby instance, navigate to **Admin > High Availability > Replication Configuration** and select **Standby**, as per the following illustration:

    ![Standby instance type](/images/yp/high-availability/standby-configuration.png)

1. Enter the instance's IP address or hostname, including the HTTP or HTTPS protocol prefix and port if you are not using the default of 80 or 443.

1. Paste the shared authentication key from the active instance into the **Shared Authentication Key** field.

1. Click **Create**.

1. If the standby instance is using the HTTPS protocol and self-signed certificates instead of a trusted Certificate Authority (CA), you need to get the CA certificate for this YBA instance.

    - If you installed YBA using [YBA Installer](../../install-yugabyte-platform/install-software/installer/), locate the CA certificate from the path `/opt/yugabyte/data/yba-installer/certs/ca_cert.pem` on the YBA instance. You may need to replace `/opt/yugabyte` with the path to a custom install root if you configured yba installer using the [configuration options](../../install-yugabyte-platform/install-software/installer/#configuration-options).

    - If you installed YBA using [Replicated](../../install-yugabyte-platform/install-software/replicated/), locate the CA certificate from the path `/var/lib/replicated/secrets/ca.crt` on the YBA instance.

1. Switch to the active instance.

1. Add the standby instance root certificate to the [YugabyteDB Anywhere trust store](../../security/enable-encryption-in-transit/#add-certificates-to-your-trust-store) on the active instance. **Note**  that you need to perform this step on the active instance, and not the standby instance.

    This allows the primary to connect to the standby instance.

1. Navigate to **Admin > High Availability > Replication Configuration** and select **Instance Configuration**.

1. Click **Add Instance**, enter the new standby instance's IP address or hostname, including the HTTP or HTTPS protocol prefix and port if you are not using the default of 80 or 443.

1. Click **Continue** on the **Add Standby Instance** dialog.

1. Switch back to the new standby instance, wait for a replication interval to pass, and then refresh the page. The other instances in the HA cluster should now appear in the list of instances.

Your standby instance is now configured.

### Verify HA

To confirm communication between the active and standby, you can do the following:

- Click **Make Active** on the standby. You should see a list of available backups that you can restore from. (Don't promote the standby.)
- Verify that Prometheus on the standby is able to see similar metrics to the active. Navigate to `http://<STANDBY_IP>:9090/targets`; the federate target should have a status of UP, and the endpoint should match the active instance IP address.

    ![Verify Prometheus](/images/yp/high-availability/ha-prometheus.png)

- Verify that the standby has all the database releases that are in use by universes also listed as Active on the **Releases** page (navigate to **Profile > Releases**). To discover all the database releases that are in use by universes, you can view the **Dashboard** page.

    If releases are missing, follow the instructions in [How to configure YugabyteDB Anywhere to provide Older, Hotfix, or Debug Builds](https://support.yugabyte.com/hc/en-us/articles/360054421952-How-to-configure-YugabyteDB-Anywhere-to-provide-Older-Hotfix-or-Debug-Builds).

If the YBA instances are configured to use the HTTPS protocol and you are having problems, verify that certificates and ports are set up correctly. If the issue persists, consider relaxing the certificate validation requirements as a workaround, by enabling the [runtime configuration](../manage-runtime-config/) `yb.ha.ws.ssl.loose.acceptAnyCertificate` (set the flag to `true`).

## Promote a standby instance to active

You can make a standby instance active as follows:

1. On the standby instance you want to promote, navigate to **Admin > High Availability > Replication Configuration** and click **Make Active**.

1. Select the backup from which you want to restore (in most cases, you should choose the most recent backup) and enable **Confirm promotion**.

1. Click **Continue**. The restore takes a few seconds, after which expect to be signed out.

1. Sign in using the credentials that you had configured on the previously active instance.

You should be able to see that all of the data has been restored into the instance, including universes, users, metrics, alerts, task history, cloud providers, and so on.

### Verify promotion

After switching or failing over to the standby, verify that the old active YBA instance is in standby mode (switchover) or no longer available (failover). If both YBA instances were to attempt to perform actions on a universe, it could have unpredictable side effects.

#### Switchover

After a switchover, do the following:

- [Verify that HA is functioning properly](#verify-ha).
- If the old active instance is not in standby mode, there could be an issue with communication from the new active to the old active instance. Follow the [setup instructions](#configure-active-and-standby-instances) to verify that certificates and ports are set up correctly.

#### Failover

After a failover, do the following:

- If the old active instance is hard down, verify that there is no chance that it can come back and run YBA at a later point. It is recommended to re-image the server hosting the active instance.
- If the old active instance does come back up, it should automatically go into standby mode. If it does not go into standby mode, you should manually demote it using the YBA API. Refer to [High Availability Workflows](https://github.com/yugabyte/yugabyte-db/blob/master/managed/api-examples/python-simple/high-availability.ipynb) for an example.

    If you can't identify the timestamp for whatever reason, you can provide a current timestamp to forcibly demote the instance. However, it will be harder to reestablish the HA connection as the active and standby will have different notions of the last failover time. This could lead to unexpected behavior.

    If there are any issues with the old active going into standby that you can't resolve, you should disable HA completely and re-enable it with the new active-standby configuration.

- If the old active instance has successfully switched to standby, [verify that HA is functioning properly](#verify-ha).

## Upgrade instances

All instances involved in HA should use the same version of YugabyteDB Anywhere. Promoting a standby instance using a YBA backup from an active instance where the YBA versions are different may result in errors.

Although you can upgrade all YBA instances simultaneously and there are no explicit ordering requirements regarding upgrades of active and standby instances, you should follow these general guidelines:

- Start an upgrade with the active instance.
- After the active instance has been upgraded, ensure that YugabyteDB Anywhere is reachable by signing in and checking various pages.
- Proceed with upgrading standby instances.
- After upgrading all instances, verify that the standbys are receiving new backups from the active instance.

Certificates in the trust store should not require setup again.

{{< warning title="Don't promote an old active backup" >}}

Immediately after an upgrade, older backups of the active instance _before it was upgraded_ will still be available on the standby. These are not deleted until the standby is promoted at some point, or they expire. Because these old backups are present, you need to be cautious promoting the standby in the time immediately after upgrading the HA pair.

Only promote standby when both standby and active are at same version, and use the most recent backup that you are confident was received after the active was upgraded.

{{< /warning >}}

### Upgrade instances in HA

To upgrade instances in a HA cluster, do the following:

1. Stop the HA synchronization. This ensures that only backups of the original YugabyteDB Anywhere version are synchronized to the standby instance.
1. [Upgrade the active instance](../../upgrade/). Expect a momentary lapse in availability for the duration of the upgrade.

    If the upgrade is successful, proceed to step 3.

    If the upgrade fails, perform the following:

    - Decommission the faulty active instance in the active-standby pair.
    - [Promote the standby instance](#promote-a-standby-instance-to-active).
    - Do not attempt to upgrade until the root cause of the upgrade failure is determined.
    - Delete the HA configuration and bring up another standby instance at the original YugabyteDB Anywhere version and reconfigure HA.
    - After the root cause of failure has been established, repeat the upgrade process starting from step 1. Depending on the cause of failure and its solution, this may involve upgrading to a different version of YugabyteDB Anywhere.

1. On the upgraded instance, perform post-upgrade validation tests that may include creating or editing a universe, backups, and so on.
1. [Upgrade the standby instance](../../upgrade/).
1. Enable HA synchronization.
1. Optionally, promote the standby instance with the latest backup synchronized from the YugabyteDB Anywhere version to which to upgrade.

The following diagram provides a graphical representation of the upgrade procedure:

![High availability upgrade](/images/yp/high-availability/ha-upgrade.png)

Where Version A is the original YugabyteDB Anywhere version present on the active and standby instances, and Version B is the version you are upgrading to.

## Remove a standby instance

To remove a standby instance from a HA cluster, you need to remove it from the active instance's list, and then delete the configuration from the instance to be removed, as follows:

1. On the active instance's list, click **Delete Instance** for the standby instance to be removed.

1. On the standby instance you wish to remove from the HA cluster, on the **Admin > High Availability** tab, click **Delete Configuration**.

The standby instance is now a standalone instance again.

After you have returned a standby instance to standalone mode, the information on the instance is likely to be out of date, which can lead to incorrect behavior. It is recommended to wipe out the state information before using it in standalone mode. For assistance with resetting the state of a standby instance that you removed from a HA cluster, contact Yugabyte Support.

## Limitations

- No automatic failover. If the active instance fails, follow the steps in [Promote a standby instance to active](#promote-a-standby-instance-to-active).
- The last backup time updates regardless of successful synchronization. To validate that backups are running, check the YBA logs for errors during synchronization.
- After promotion, you may be unable to sign in to the new active instance for under a minute. You can wait and then reload the page, or you can restart the newly active YBA.
