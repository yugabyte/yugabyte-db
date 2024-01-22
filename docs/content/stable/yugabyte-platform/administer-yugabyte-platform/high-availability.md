---
title: High availability of YugabyteDB Anywhere
headerTitle: Enable high availability
description: Make YugabyteDB Anywhere highly available
headcontent: Configure standby instances of YugabyteDB Anywhere
linkTitle: Enable high availability
menu:
  stable_yugabyte-platform:
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
- YugabyteDB Anywhere VMs can connect to each other over the port where the YugabyteDB Anywhere UI is reachable (typically 443).
- Communication is open in both directions over ports 9000 and 9090 on all instances.
- If you are using custom ports for Prometheus, all YugabyteDB Anywhere instances are using the same custom port. The default Prometheus port for YugabyteDB Anywhere is 9090.
- All YugabyteDB Anywhere instances are running the same version of YugabyteDB Anywhere software. You should upgrade all YugabyteDB Anywhere instances in the HA cluster at approximately the same time.

## Configure active and standby instances

To set up HA for YugabyteDB Anywhere, you first configure the active instance by creating an active HA replication configuration and generating a shared authentication key.

You then configure one or more standby instances by creating standby HA replication configurations, using the shared authentication key generated on the active instance.

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

To confirm communication between the active and standby, click **Make Active** on the standby. You should see a list of available backups that you can restore from.

During a HA backup, the entire YugabyteDB Anywhere state is copied. If your universes are visible through YugabyteDB Anywhere UI and the replication timestamps are increasing, the backup is successful.

## Promote a standby instance to active

You can make a standby instance active as follows:

1. Open **Replication Configuration** of the standby instance that you wish to promote to active and click **Make Active**.

1. Use the **Make Active** dialog to select the backup from which you want to restore (in most cases, you should choose the most recent backup) and enable **Confirm promotion**.

1. Click **Continue**. The restore takes a few seconds, after which expect to be logged out.

1. Log in using credentials that you had configured on the previously active instance.

You should be able to see that all of the data has been restored into the instance, including universes, users, metrics, alerts, task history, cloud providers, and so on.

### Verify failover or switchover

After switching or failing over to the standby, verify that the old active is in standby mode (switchover) or no longer available. If both YBA instances attempt to take actions on DB universes, it could cause unpredictable side effects.

- If it is not in standby mode, there could be an issue with communication from new active -> old active. Follow the setup instructions to verify that certificates and ports have been set up correctly.
- If the old active is hard down (failover), verify that there is no chance for the old active to come back and run YBA at a later point.
- If the old active does come back up, it should automatically go into standby mode. If the old active instance does not go into standby mode, you should manually demote it as follows:

    1. Find the correct last failover time by querying the true active instance.

        ```sh
        curl -X GET 'https://10.9.104.164/api/v1/settings/ha/config' -H 'X-AUTH-YW-API-TOKEN: 8bf6e5b2-beff-4859-9016-bdc0fa0ced1e' --insecure
        ```

    1. To the stale active send a manual demote request.

        ```sh
        curl -X PUT 'https://10.9.113.130/api/v1/settings/ha/internal/config/demote/1705138628028' -H 'X-AUTH-YW-API-TOKEN: 1a5ad6de-f638-4ebc-9649-d8b187b688b0' -H 'HA-AUTH-TOKEN: sTroAGbJz+QydXXz9lc1bhdQmQqIZSyM6Z20MqitvLA=' -H "Content-Type: application/json" --data-raw '{"leader_address": "https://10.9.104.164"}' --insecure
        ```

    If the timestamp can not be found for whatever reason, providing a current timestamp will forcibly demote the instance, but it will be harder to reestablish HA connection as now the active/standby have different notions of the last failover time. This could lead to unexpected behavior.

If there are any issues with the old active going into standby that cannot be resolved by the above steps, it is recommended to disable HA completely and re-enable it with the new active standby configuration.

Verify that backups are flowing to the new standby (old active).

Verify that Prometheus on standby is able to see similar metrics as the active

Verify that new active has all the DB releases in use listed as active from the Releases page.

To discover all in use DB releases, you can view the YBA dashboard page

To do the same programmatically, you can use the following query (need jq command line tool)
curl 'https://10.9.113.130/api/v1/customers/<CUSTOMER UUID>/universes' -H 'X-AUTH-YW-API-TOKEN: <YBA API TOKEN>'  --compressed --insecure | jq -r '.[].universeDetails.clusters | map(.userIntent.ybSoftwareVersion) | join(", ")'
Search for all the returned releases in the releases page, if any don't show up or are not active, follow the process in https://support.yugabyte.com/hc/en-us/articles/360054421952-How-to-configure-YugabyteDB-Anywhere-to-provide-Older-Hotfix-or-Debug-Builds to add the appropriate releases to the new active instance.

## Upgrade instances

All instances involved in HA should be of the same YugabyteDB Anywhere version. If the versions are different, an attempt to promote a standby instance using a YugabyteDB Anywhere backup from an active instance may result in errors.

Even though you can perform an upgrade of all YugabyteDB Anywhere instances simultaneously and there are no explicit ordering requirements regarding upgrades of active and standby instances, it is recommended to follow these general guidelines:

- Start an upgrade with an active instance.
- After the active instance has been upgraded, ensure that YugabyteDB Anywhere is reachable by logging in and checking various pages.
- Proceed with upgrading standby instances.

The following is the detailed upgrade procedure:

1. Stop the HA synchronization. This ensures that only backups of the original YugabyteDB Anywhere version are synchronized to the standby instance.
1. [Upgrade the active instance](../../upgrade/). Expect a momentary lapse in availability for the duration of the upgrade. If the upgrade is successful, proceed to step 3. If the upgrade fails, perform the following:

    - Decommission the faulty active instance in the active-standby pair.
    - Promote the standby instance.
    - Do not attempt to upgrade until the root cause of the upgrade failure is determined.
    - Delete the HA configuration and bring up another standby instance at the original YugabyteDB Anywhere version and reconfigure HA.
    - After the root cause of failure has been established, repeat the upgrade process starting from step 1. Depending on the cause of failure and its solution, this may involve a different YugabyteDB Anywhere version to which to upgrade.

1. On the upgraded instance, perform post-upgrade validation tests that may include creating or editing a universe, backups, and so on.
1. [Upgrade the standby instance](../../upgrade/).
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

1. On the standby instance you wish to remove from the HA cluster, on the **Admin > High Availability** tab, click **Delete Configuration**.

The standby instance is now a standalone instance again.

After you have returned a standby instance to standalone mode, the information on the instance is likely to be out of date, which can lead to incorrect behavior. It is recommended to wipe out the state information before using it in standalone mode. For assistance with resetting the state of a standby instance that you removed from a HA cluster, contact Yugabyte Support.

## Troubleshooting

If you face issues configuring high availability when the YBA instances are configured to use the HTTPS protocol, attempt the steps mentioned in the preceding sections to add CA certificates appropriately to the trust store. If the issue persists, consider relaxing the certificate validation requirements as a workaround, by enabling the runtime configuration `yb.ha.ws.ssl.loose.acceptAnyCertificate` (set the flag to `true`).
