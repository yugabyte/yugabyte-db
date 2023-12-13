---
title: Install YugabyteDB Anywhere software - Installer
headerTitle: Install YugabyteDB Anywhere
linkTitle: Install YBA software
description: Install YugabyteDB Anywhere software using YBA Installer
headContent: Install YBA software using YBA Installer
aliases:
  - /preview/yugabyte-platform/install-yugabyte-platform/install-software/
menu:
  preview_yugabyte-platform:
    parent: install-yugabyte-platform
    identifier: install-software-4-installer
    weight: 77
rightNav:
  hideH4: true
type: docs
---

Use the following instructions to install YugabyteDB Anywhere (YBA) software. For guidance on which method to choose, see [YBA prerequisites](../../prerequisites/installer/).

Note: For higher availability, one or more additional YBA instances can be separately installed, and then configured later to serve as passive warm standby servers. See [Enable High Availability](../../../administer-yugabyte-platform/high-availability/) for more information.

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../installer/" class="nav-link active">
      <i class="fa-solid fa-building"></i>YBA Installer</a>
  </li>

  <li>
    <a href="../default/" class="nav-link">
      <i class="fa-solid fa-cloud"></i>Replicated</a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>Kubernetes</a>
  </li>

  <li>
    <a href="../openshift/" class="nav-link">
      <i class="fa-brands fa-redhat"></i>OpenShift</a>
  </li>

</ul>

Use YBA Installer to install YBA on a host, either online or airgapped. YBA Installer performs preflight checks to validate the workspace is ready to run YBA.

You can also use YBA Installer to migrate an existing YBA software installed via Replicated to be installed using YBA Installer {{<badge/ea>}}. Before you can migrate, you must use Replicated to upgrade your YBA to version 2.20.1.

- To perform a new installation, follow the steps in [Quick start](#quick-start).

- To upgrade an installation of YBA that was installed using YBA Installer, refer to [Upgrade](#upgrade).

- To migrate an installation from Replicated, refer to [Migrate from Replicated](#migrate-from-replicated).

After the installation is complete, you can use YBA Installer to manage your installation. This includes backup and restore, upgrading, basic licensing, and uninstalling the software.

## Prerequisites

- Ensure your machine satisfies the [minimum requirements](../../prerequisites/installer/).
- For production deployments, sudo permissions are required for some YBA Installer commands. (You can use YBA Installer without sudo access, but this is not recommended for production; refer to [Non-sudo installation](#non-sudo-installation).)

  If your sudo permissions are limited, add the following to the allowed list for root in the sudoers file:

  ```sh
  /bin/mv, /usr/bin/find, /opt/yugabyte/software/*/pgsql/bin/createdb, /opt/yugabyte/software/*/pgsql/bin/initdb
  ```

## Quick start

To install YugabyteDB Anywhere using YBA Installer, do the following:

1. Obtain your license from Yugabyte Support.
1. Download and extract the YBA Installer by entering the following commands:

    ```sh
    $ wget https://downloads.yugabyte.com/releases/{{<yb-version version="stable" format="long">}}/yba_installer_full-{{<yb-version version="stable" format="build">}}-linux-x86_64.tar.gz
    $ tar -xf yba_installer_full-{{<yb-version version="stable" format="build">}}-linux-x86_64.tar.gz
    $ cd yba_installer_full-{{<yb-version version="stable" format="build">}}/
    ```

1. Using sudo, run a preflight check to ensure your environment satisfies the requirements. Respond with `y` when prompted to create a default configuration.

    ```sh
    $ sudo ./yba-ctl preflight
    ```

1. If there are no issues, using sudo, install the software, providing your license.

    ```sh
    $ sudo ./yba-ctl install -l /path/to/license
    ```

After the installation succeeds, you can immediately start using YBA.

If the installation fails due to permissions or lack of sudo privileges, you can retry after running `yba-ctl clean all` to remove all traces of the previous attempt.

For more detailed installation instructions and information on how to use YBA Installer to manage your installation, refer to the following sections.

## Download and configure YBA Installer

### Download YBA Installer

Download and extract the YBA Installer by entering the following commands:

```sh
$ wget https://downloads.yugabyte.com/releases/{{<yb-version version="preview" format="long">}}/yba_installer_full-{{<yb-version version="preview" format="build">}}-linux-x86_64.tar.gz
$ tar -xf yba_installer_full-{{<yb-version version="preview" format="build">}}-linux-x86_64.tar.gz
$ cd yba_installer_full-{{<yb-version version="preview" format="build">}}/
```

This bundle provides everything needed, except a [license](#provide-a-license), to complete a fresh install of YBA:

- `yba-ctl` executable binary is used to perform all of the YBA Installer workflows.
- `yba-ctl.yml.reference` is a YAML reference for the available configuration options for both YBA Installer and YugabyteDB Anywhere.

To see a full list of commands, run the following command:

```sh
$ ./yba-ctl help
```

yba-ctl commands need to be run in the correct context; see [Running yba-ctl commands](#running-yba-ctl-commands).

### Configure YBA Installer

Many YBA Installer commands require a configuration file, including `preflight` and `install`. When using these commands without a configuration file, you are prompted to continue using default values. For example:

```sh
$ sudo ./yba-ctl preflight
```

```output
No config file found at '/opt/yba-ctl/yba-ctl.yml', creating it with default values now.
Do you want to proceed with the default config? [yes/NO]:
```

Respond with `y` or `yes` to create the configuration file using default configuration settings and continue the operation.

Respond with `n` or `no` to create the configuration file using default configuration settings and exit the command.

By default, YBA Installer installs YBA in `/opt/yugabyte` and creates a Linux user `yugabyte` to run YBA processes.

To change these and other default values, edit the `yba-ctl.yml` file, and then re-run the `yba-ctl` command. For a list of options, refer to [Configuration options](#configuration-options).

You can change some configuration options post-installation using the [reconfigure](#reconfigure) command.

## Install YBA using YBA Installer

### Provide a license

YBA Installer requires a valid license before installing. To obtain a license, contact {{% support-platform %}}.

Provide the license to YBA Installer by running the `license` command as follows:

```sh
$ sudo ./yba-ctl license add -l /path/to/license
```

You can use this command to update to a new license if needed.

You can also provide a license when running the `install` command. Refer to [Install the software](#install-the-software).

### Run preflight checks

Start by running the preflight checks to ensure that the expected ports are available, the hardware meets the [minimum requirements](../../prerequisites/installer/), and so forth. The preflight check generates a report you can use to fix any issues before continuing with the installation.

```sh
$ sudo ./yba-ctl preflight
```

```output
#  Check name             Status   Error
1  license                Critical stat /opt/yba-ctl/YBA.lic: no such file or directory
2  install does not exist Pass
3  validate-config        Pass
4  user                   Pass
5  cpu                    Pass
6  memory                 Pass
7  port                   Pass
8  python                 Pass
9  disk-availability      Pass
10 postgres               Pass
```

Some checks, such as CPU or memory, can be skipped, though this is not recommended for a production installation. Others, such as having a license and python installed, are hard requirements, and YugabyteDB Anywhere can't work until these checks pass. All checks should pass for a production installation.

If you are installing YBA for testing and evaluation and you want to skip a check that is failing, you can pass `–skip_preflight <name>[,<name2>]`. For example:

```sh
$ sudo ./yba-ctl preflight --skip_preflight cpu
```

### Install the software

To perform an install, run the `install` command. Once started, an install can take several minutes to complete.

```sh
$ sudo ./yba-ctl install
```

You can also provide a license when running the `install` command by using the `-l` flag if you haven't [set the license prior to install](#provide-a-license) :

```sh
$ sudo ./yba-ctl install -l /path/to/license
```

```output
               YBA Url |   Install Root |            yba-ctl config |              yba-ctl Logs |
  https://10.150.0.218 |  /opt/yugabyte |  /opt/yba-ctl/yba-ctl.yml |  /opt/yba-ctl/yba-ctl.log |

Services:
  Systemd service |       Version |  Port |                            Log File Locations |  Running Status |
         postgres |         10.23 |  5432 |          /opt/yugabyte/data/logs/postgres.log |         Running |
       prometheus |        2.42.0 |  9090 |  /opt/yugabyte/data/prometheus/prometheus.log |         Running |
      yb-platform |  {{<yb-version version="preview" format="build">}} |   443 |       /opt/yugabyte/data/logs/application.log |         Running |
INFO[2023-04-24T23:19:59Z] Successfully installed YugabyteDB Anywhere!
```

The `install` command runs all [preflight checks](#run-preflight-checks) first, and then proceeds to do a full install, and then waits for YBA to start. After the install succeeds, you can immediately start using YBA.

## Migrate from Replicated

If your YBA installation uses Replicated, you can use YBA Installer to migrate from Replicated. {{<badge/ea>}}

{{< note title="Replicated end of life" >}}

YugabyteDB Anywhere will end support for Replicated installation at the end of 2024.

{{< /note >}}

Review the [prerequisites](#prerequisites). YBA Installer performs the migration in place. Make sure you have enough disk space on your current machine.

### Migrate a YBA installation

To migrate your installation from Replicated, do the following:

1. If your Replicated installation is v2.18.5 or earlier, [upgrade your installation](../../../upgrade/upgrade-yp-replicated/) to v2.20.1.

1. If you haven't already, [download and extract YBA Installer](#download-yba-installer).

1. Optionally, configure the migration as follows:

    ```sh
    $ sudo ./yba-ctl replicated-migrate config
    ```

    This generates a configuration file `/opt/yba-ctl/yba-ctl.yml` with the settings for your current installation, which are used for the migration. You can edit the file to customize the install further. For a list of options, refer to [Configuration options](#configuration-options).

1. Start the migration, passing in your license file:

    ```sh
    $ sudo ./yba-ctl replicated-migrate start -l /path/to/license
    ```

    The `start` command runs all [preflight checks](#run-preflight-checks) and then proceeds to do the migration, and then waits for YBA to start. After the migration finishes, YBA is installed.

1. Validate YBA is up and running with the correct data, including Prometheus.

    At this point, if you find problems, you can revert to your Replicated installation using the `replicated-migrate rollback` command. Note that any changes made with the new YBA (either using the UI or the API) will not be reflected after the rollback.

1. If the new YBA installation is correct, finish the migration as follows:

    ```sh
    $ sudo ./yba-ctl replicated-migrate finish
    ```

    This uninstalls Replicated and makes the new YBA instance permanent.

### Migration and high availability

If you have [high availability](../../../administer-yugabyte-platform/high-availability/) (HA) configured, migrate your installations as follows.

If Replicated is using HTTPS:

1. If your instances are v2.18.5 or earlier, [upgrade your primary and HA standby instances](../../../administer-yugabyte-platform/high-availability/#upgrade-instances) to v2.20.1.
1. Migrate and finalize the primary.
1. Migrate and finalize the standby.

Failovers are now possible again after the completion of this step.

If Replicated is using HTTP:

1. [Remove the standby instances](../../administer-yugabyte-platform/high-availability/#remove-a-standby-instance).
1. If your instances are v2.18.5 or earlier, [upgrade the primary and standby instances](../../../administer-yugabyte-platform/high-availability/#upgrade-instances) to v2.20.1.
1. Migrate and finalize the primary.
1. Migrate and finalize the standbys.
1. [Configure HA on the updated instances](../../administer-yugabyte-platform/high-availability/#configure-active-and-standby-instances).

Failovers are now possible again after the completion of this step.

## Manage a YBA installation

### Reconfigure

YBA Installer can be used to reconfigure an installed YBA instance.

To reconfigure an installation, edit the `/opt/yba-ctl/yba-ctl.yml` configuration file with your changes, and then run the command as follows:

```sh
$ sudo yba-ctl reconfigure
```

For a list of options, refer to [Configuration options](#configuration-options). Note that some settings can't be reconfigured, such as the install root, service username, or the PostgreSQL version.

### Service management

YBA Installer provides basic service management, with `start`, `stop`, and `restart` commands. Each of these can be performed for all the services (`platform`, `postgres`, and `prometheus`), or any individual service.

```sh
$ sudo yba-ctl [start, stop, reconfigure]
$ sudo yba-ctl [start, stop, reconfigure] prometheus
```

In addition to the state changing operations, you can use the `status` command to show the status of all YugabyteDB Anywhere services, in addition to other information such as the log and configuration location, versions of each service, and the URL to access the YugabyteDB Anywhere UI.

```sh
$ sudo yba-ctl status
```

```output
               YBA Url |   Install Root |            yba-ctl config |              yba-ctl Logs |
  https://10.150.0.218 |  /opt/yugabyte |  /opt/yba-ctl/yba-ctl.yml |  /opt/yba-ctl/yba-ctl.log |

Services:
  Systemd service |       Version |  Port |                            Log File Locations |  Running Status |
         postgres |         10.23 |  5432 |          /opt/yugabyte/data/logs/postgres.log |         Running |
       prometheus |        2.42.0 |  9090 |  /opt/yugabyte/data/prometheus/prometheus.log |         Running |
      yb-platform |  {{<yb-version version="preview" format="build">}} |   443 |       /opt/yugabyte/data/logs/application.log |         Running |
```

### Upgrade

To upgrade using YBA Installer, first download the version of YBA Installer corresponding to the version of YBA you want to upgrade to. See [Download YBA Installer](#download-yba-installer).

Upgrade works similarly to the install workflow, by first running preflight checks to validate the system is in a good state.

When ready to upgrade, run the `upgrade` command from the untarred directory of the target version of the YBA upgrade:

```sh
$ sudo ./yba-ctl upgrade
```

The upgrade takes a few minutes to complete. When finished, use the [status command](#service-management) to verify that YBA has been upgraded to the target version.

### Backup and restore

YBA Installer also provides utilities to take full backups of the YBA state (not YugabyteDB however) and later restore from them. This not only includes data seen in YBA for your universes, but also metrics stored in Prometheus.

To perform a backup, provide the full path to the directory where the backup will be generated. The `createBackup` command creates a timestamped `tgz` file for the backup. For example:

```sh
$ sudo yba-ctl createBackup ~/test_backup
$ ls test_backup/
```

```output
backup_23-04-25-16-54.tgz  version_metadata_backup.json
```

To restore from the same backup, use the `restoreBackup` command:

```sh
$ sudo yba-ctl restoreBackup ~/test_backup/backup_23-04-25-16-64.tgz
```

### Clean (uninstall)

To uninstall a YBA instance, YBA Installer also provides a `clean` command.

By default, `clean` removes the YugabyteDB Anywhere software, but keeps any data such as PostgreSQL or Prometheus information:

```sh
$ sudo yba-ctl clean
```

```output
INFO[2023-04-24T23:58:13Z] Uninstalling yb-platform
INFO[2023-04-24T23:58:14Z] Uninstalling prometheus
INFO[2023-04-24T23:58:14Z] Uninstalling postgres
```

To delete all data, run `clean` with the `–-all` flag as follows:

```sh
$ sudo yba-ctl clean --all
```

```output
--all was specified. This will delete all data with no way to recover. Continue? [yes/NO]: y
INFO[2023-04-24T23:58:13Z] Uninstalling yb-platform
INFO[2023-04-24T23:58:14Z] Uninstalling prometheus
INFO[2023-04-24T23:58:14Z] Uninstalling postgres
```

## Running yba-ctl commands

YBA Installer commands are run in the following contexts:

- local execution path using `./yba-ctl`
- installed execution path using `yba-ctl`

This is because some commands require local execution context, while others require the context of the installed system.

The following commands must be run using the local execution path:

- `install`
- `upgrade`

The following commands must be run using the installed execution path:

- `createBackup`
- `restoreBackup`
- `clean`
- `start`, `stop`, `restart`, and `status`
- `reconfigure`

The `help`, `license`, and `preflight` commands can be run in either context.

If you don't use the correct execution path, yba-ctl fails with an error:

```sh
$ sudo ./yba-ctl createBackup ~/backup.tgz
```

```output
FATAL[2023-04-25T00:14:57Z] createBackup must be run from the installed yba-ctl
```

## Non-sudo installation

YBA Installer also supports a non-sudo installation, where sudo access is not required for any step of the installation. Note that this is not recommended for production use cases.

To facilitate a non-sudo install, YBA Installer will not create any additional users or set up services in systemd. The install will also be rooted in the home directory by default, instead of /opt, ensuring YBA Installer has write access to the base install directory. Instead of using systemd to manage services, basic cron jobs are used to start the services on bootup with basic management scripts used to restart the services after a crash.

To perform a non-sudo installation, run any of the preceding commands without sudo access. You can't switch between a sudo and non-sudo access installation, and `yba-ctl` will return an error if sudo is not used when operating in an installation where sudo access was used.

## Configuration options

### YBA Installer configuration options

You can set the following YBA Installer configuration options.

| Option | Description |
| :--- | :--- |
| `installRoot` | Location where YBA is installed. Default is `/opt/yugabyte`. |
| `host` | Hostname or IP Address used for CORS and certificate creation. Optional. |
| `server_cert_path`<br />`server_key_path` | If providing custom certificates, give the path with these values. If not provided, the installation process generates self-signed certificates. Optional. |
| `service_username` | The Linux user that will run the YBA processes. Default is `yugabyte`. The install process will create the `yugabyte` user. If you wish to use a different user, create that user beforehand and specify it in `service_username`. YBA Installer only creates the `yugabyte` user, not custom usernames. |

### YBA configuration options

You can configure the following YBA configuration options.

| Option | Description |
| :--- | :--- |
| `port` | Specify a custom port for the YBA UI to run on.
| `keyStorePassword` | Password for the Java keystore. Auto-generated if left empty.
| `appSecret` | Play framework crypto secret. Auto-generated if left empty.

OAuth related settings are described in the following table. Only set these fields if you intend to use OIDC SSO for your YugabyteDB Anywhere installation (otherwise leave it empty).

| Option | Description |
| :--- | :--- |
| `useOauth` | Boolean that determines if OIDC SSO needs to be enabled for YBA. Default is false. Set to true if you intend on using OIDC SSO for your YBA installation (must be a boolean).
| `ybSecurityType` | The Security Type corresponding to the OIDC SSO for your YBA installation.
| `ybOidcClientId` | The Client ID corresponding to the OIDC SSO for your YBA installation.
| `ybOidcSecret` | The OIDC Secret Key corresponding to the OIDC SSO for your YBA installation.
| `ybOidcDiscoveryUri` | The OIDC Discovery URI corresponding to the OIDC SSO for your YBA installation. Must be a valid URL.
| `ywWrl` | The Platform IP corresponding to the OIDC SSO for your YBA installation. Must be a valid URL.
| `ybOidcScope` | The OIDC Scope corresponding to the OIDC SSO for your YBA installation.
| `ybOidcEmailAtr` | The OIDC Email Attribute corresponding to the OIDC SSO for your YBA installation. Must be a valid email address.

Http and Https proxy settings are described in the following table.

| Option | Description |
| :--- | :--- |
| `http_proxy` |            Specify the setting for HTTP_PROXY
| `java_http_proxy_port` |  Specify -Dhttp.proxyPort
| `java_http_proxy_host` |  Specify -Dhttp.proxyHost
| `https_proxy` |           Specify the setting for HTTPS_PROXY
| `java_https_proxy_port` | Specify -Dhttps.proxyPort
| `java_https_proxy_host` | Specify -Dhttps.proxyHost
| `no_proxy` |              Specify the setting for NO_PROXY
| `java_non_proxy` |        Specify  -Dhttps.nonProxyHosts.

### Prometheus configuration options

| Option | Description |
| :--- | :--- |
| `port` | External Prometheus port
| `restartSeconds` | Systemd will restart Prometheus after this number of seconds after a crash.
| `scrapeInterval` | How often Prometheus scrapes for database metrics.
| `scrapeTimeout` | Timeout for inactivity during scraping.
| `maxConcurrency` | Maximum concurrent queries to be executed by Prometheus.
| `maxSamples` | Maximum number of samples that a single query can load into memory.
| `timeout` | The time threshold for inactivity after which Prometheus will be declared inactive.

### Configure PostgreSQL

By default, YBA Installer provides a version of PostgreSQL. If you prefer, you can use your own version of PostgreSQL.

PostgreSQL configuration is divided into two different subsections:

- `install` - contains information on how YBA Installer should install PostgreSQL.
- `useExisting` - provides YBA Installer with information on how to connect to a PostgreSQL instance that you provision and manage separately.

These options are mutually exclusive, and can be turned on or off using the _enabled_ option. Exactly one of these two sections must have enabled = true, while the other must have enabled = false.

**Install options**

| Option | Description |
| :--- | :--- |
| `port` | Port PostgreSQL is listening to.
| `restartSecond` | Wait time to restart PostgreSQL if the service crashes.
| `locale` | locale is used during initialization of the db.

**useExisting options**

| Option | Description |
| :--- | :--- |
| `host` | IP address/domain name of the PostgreSQL server.
| `port` | Port PostgreSQL is running on.
| `username` and `password` | Used to authenticate with PostgreSQL.
| `pg_dump_path`<br/>`pg_restore_path` | Required paths to `pgdump` and `pgrestore` on the locale system that are compatible with the version of PostgreSQL you provide. `pgdump` and `pgrestore` are used for backup and restore workflows, and are required for a functioning install.
