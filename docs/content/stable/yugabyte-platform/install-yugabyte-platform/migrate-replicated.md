---
title: Migrate from Replicated
headerTitle: Migrate from Replicated
linkTitle: Migrate from Replicated
description: Migrate from Replicated.
menu:
  stable_yugabyte-platform:
    identifier: migrate-replicated
    parent: install-yugabyte-platform
    weight: 50
type: docs
---

YugabyteDB Anywhere will end support for Replicated installation at the end of 2024. If your YBA installation uses Replicated, you can use [YBA Installer](../install-software/installer/) to migrate from Replicated.

## Before you begin

- Review the [prerequisites](../../prepare/).
- YBA Installer can perform the migration in place. Make sure you have enough disk space on your current machine for both the Replicated and YBA Installer installations.
- If your Replicated installation is v2.18.5 or earlier, or v2.20.0, [upgrade your installation](../../upgrade/upgrade-yp-replicated/) to v2.20.1.3 or later.
- If you haven't already, [download and extract](../install-software/installer/#download-yba-installer) YBA Installer. It is recommended that you migrate using the same version of YBA Installer as the version of YBA you are running in Replicated. For example, if you have v.{{<yb-version version="stable" format="long">}} installed, use the following commands:

    ```sh
    $ wget https://downloads.yugabyte.com/releases/{{<yb-version version="stable" format="long">}}/yba_installer_full-{{<yb-version version="stable" format="build">}}-linux-x86_64.tar.gz
    $ tar -xf yba_installer_full-{{<yb-version version="stable" format="build">}}-linux-x86_64.tar.gz
    $ cd yba_installer_full-{{<yb-version version="stable" format="build">}}/
    ```

If you have high availability configured, you need to migrate your instances in a specific order. See [Migration and high availability](#migration-and-high-availability).

## Migrate a Replicated installation

You can migrate your existing Replicated installation to YBA Installer in place on the same VM (recommended). When migrating in place, both your Replicated configuration and the YBA metadata (universes, providers, and so on) are migrated to the YBA Installer format.

Alternately, you can migrate the [Replicated installation to YBA Installer on a different VM](#migrate-from-replicated-to-yba-installer-on-a-different-vm). Only the YBA metadata (universes, providers, and so on) is migrated to the new VM; the Replication configuration is not.

### Migrate from Replicated to YBA Installer in place

To migrate in place, do the following:

1. Optionally, configure the migration as follows:

    ```sh
    $ sudo ./yba-ctl replicated-migrate config
    ```

    This generates a configuration file `/opt/yba-ctl/yba-ctl.yml` with the settings for your current installation, which are used for the migration. You can edit the file to customize the install further.

    Note that the `installRoot` (by default `/opt/ybanywhere`) in `yba-ctl.yml` needs to be different from the Replicated Storage Path (by default `/opt/yugabyte`). If they are set to the same value, the migration will fail. You can delete `yba-ctl.yml` and try again.

    For a list of options, refer to [Configuration options](../../install-yugabyte-platform/install-software/installer/#configuration-options).

1. Start the migration, passing in your license file:

    ```sh
    $ sudo ./yba-ctl replicated-migrate start -l /path/to/license && sudo /opt/yba-ctl/yba-ctl restart yb-platform
    ```

    The `start` command runs all [preflight checks](../../install-yugabyte-platform/install-software/installer/#run-preflight-checks) and then proceeds to do the migration, and then waits for YBA to start.

1. Validate YBA is up and running with the correct data, including Prometheus.

    If YBA does not come up or the migration has failed, you can revert to your Replicated installation using the `replicated-migrate rollback` command.

    After the new YBA comes up successfully, do not attempt to roll back to the original Replicated install of YBA. Rollback is only intended for scenarios where the migration fails. Any changes made with a new YBA (either using the UI or the API) are not reflected after a rollback.

    In particular, do not configure HA until running the `finish` command (next step) on all instances.

1. If the new YBA installation is correct, finish the migration as follows:

    ```sh
    $ sudo ./yba-ctl replicated-migrate finish
    ```

    This uninstalls Replicated and makes the new YBA instance permanent.

### Migration and high availability

If you have YBA [high availability](../../administer-yugabyte-platform/high-availability/) (HA) configured, you need to upgrade the active and standby YBA instances if they are running older versions of YBA. In addition, you need to finish migration on both the active and standby instances for failover to be re-enabled.

If Replicated is using HTTPS, migrate as follows:

1. If your instances are v2.18.5 or earlier, or v2.20.0, [upgrade your active and HA standby instances](../../administer-yugabyte-platform/high-availability/#upgrade-instances) to v2.20.1.3 or later.
1. [Migrate and finish](#migrate-a-replicated-installation) the active instance.
1. Migrate and finish the standby instances.

Failovers are only possible after you finish the migration on both the primary and standby.

If Replicated is using HTTP, you need to remove the standbys and delete the HA configuration before migrating. Migrate as follows:

1. [Remove the standby instances](../../administer-yugabyte-platform/high-availability/#remove-a-standby-instance).
1. On the active instance, navigate to **Admin > High Availability** and click **Delete Configuration**.
1. If your instances are v2.18.5 or earlier, or 2.20.0, [upgrade the primary and standby instances](../../administer-yugabyte-platform/high-availability/#upgrade-instances) to v2.20.1.3 or later.
1. [Migrate and finish](#migrate-a-replicated-installation) the active instance.
1. Migrate and finish the standby instances.
1. [Configure HA on the updated instances](../../administer-yugabyte-platform/high-availability/#configure-active-and-standby-instances).

[Failovers](../../administer-yugabyte-platform/high-availability/#failover) are possible again after the completion of this step.

### Migrate from Replicated to YBA Installer on a different VM

1. [Install YBA Installer](../install-software/installer/) on a new VM and [configure YBA Installer](../install-software/installer/#configuration-options) appropriately. Note that [Replicated configuration](../install-replicated/#set-up-https-optional) will not be migrated in this method.
2. Disable any YBA [high availability](../../administer-yugabyte-platform/high-availability/) (HA) configured on the Replicated YBA.
3. Perform a full backup on the Replicated installation. This backup includes prometheus and all existing releases. If the backup is too large, consider cleaning up unused releases through the YBA UI and/or reducing the prometheus retention interval in Replicated settings.
4. Stop YBA on the Replicated VM [through the Replicated UI or command line](../..//administer-yugabyte-platform/shutdown/#replicated). This is a critical step, otherwise you could end up with two YBA instances managing the same set of universes, which can lead to severe consequences.
5. Transfer the full backup from Step 3 to the YBA Installer VM.
6. Restore the backup on the YBA Installer VM using the command line `sudo yba-ctl restoreBackup --migration <path to backup.tar.gz>`
7. Verify the resulting YBA Installer installation contains all the YBA metadata such as universes, providers, backups etc from the original Replicated installation.
8. [Completely delete the Replicated installation](https://support.yugabyte.com/hc/en-us/articles/11095326804877-Uninstall-Replicated-based-Yugabyte-Anywhere-aka-Yugabyte-Platform-from-the-Linux-host) or re-image the Replicated VM.
9. If you had YBA [high availability](../../administer-yugabyte-platform/high-availability/) (HA) configured earlier, set up a new YBA Installer VM to be the standby and configure HA from scratch. Do not attempt to continue to use the existing Replicated standby VM.


