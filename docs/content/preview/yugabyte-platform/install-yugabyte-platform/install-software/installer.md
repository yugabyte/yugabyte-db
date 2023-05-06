---
title: Install YugabyteDB Anywhere software - Installer
headerTitle: Install YugabyteDB Anywhere software - Installer
linkTitle: Install software
description: Install YugabyteDB Anywhere software using the Installer
menu:
  preview_yugabyte-platform:
    parent: install-yugabyte-platform
    identifier: install-software-2-openshift
    weight: 88
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../default/" class="nav-link">
      <i class="fa-solid fa-cloud"></i>Default</a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>Kubernetes</a>
  </li>

  <li>
    <a href="../airgapped/" class="nav-link">
      <i class="fa-solid fa-link-slash"></i>Airgapped</a>
  </li>

  <li>
    <a href="../openshift/" class="nav-link">
      <i class="fa-brands fa-redhat"></i>OpenShift</a>
  </li>

  <li>
    <a href="../installer/" class="nav-link active">
      <i class="fa-solid fa-building"></i>Installer</a>
  </li>

</ul>

To install YugabyteDB Anywhere, you can use YugabyteDB Anywhere Installer.

## Prerequisites

Unless otherwise specified, you can use a user account for executing the steps described in this document. Using admin account for all the steps should work as well.

## Installer-based installation

To install YugabyteDB Anywhere using the Installer, do the following:

- [Download YBA Installer](#download-yba-installer)
- [Configure YBA Installer](#configure-yba-installer)
- [Run preflight checks](#run-preflight-checks)
- [Provide a license](#provide-a-license)
- [Install the software](#install-the-software)

### Download YBA Installer

Download and extract the YBA Installer by entering the following commands:

```sh
$ wget YBA_installer_url/YBA_installer_full-2.17.3.0-b123-centos-x86_64.tar.gz
$ tar -xf YBA_installer_full-2.17.3.0-b123-centos-x86_64.tar.gz

$ ls
```

```output
YBA_installer_full-2.17.3.0-b123  YBA_installer_full-2.17.3.0-b123-centos-x86_64.tar.gz
```

```sh
$ cd YBA_installer_full-2.17.3.0-b123/
$ ls
```

```output
version_metadata.json  yba-ctl  yba-ctl.yml.reference  YBA_installer-2.17.3.0-b123-linux-amd64  yugabundle-2.17.4.0-b123-centos-x86_64.tar.gz
```

This bundle provides everything needed, except a license, to complete a fresh install of YugabyteDB Anywhere:

- `yba-ctl` executable binary is used to perform all of the YBA Installer workflows
- `yba-ctl.yml.reference` is a YAML reference for the available configuration options for both YBA Installer and YugabyteDB Anywhere. Some of these configuration options cannot be changed after installation.

To see a full list of commands, run the following command:

```sh
$ ./yba-ctl help
```

### Configure YBA Installer

Many YBA Installer commands require a configuration file, including preflight and install. When using these commands with out a configuration file, you are prompted to continue using default values. For example:

```sh
$ sudo ./yba-ctl preflight
```

```output
No config file found at '/opt/yba-ctl/yba-ctl.yml', creating it with default values now.
Do you want to proceed with the default config? [yes/NO]:
```

If you respond with `y` or `yes`, the operation continues and the configuration file is created using default values at the specified location.

If you respond with `n` or `no`, the configuration file is still created, but the command exits.

To change the default values, edit the file, and then re-run the yba-ctl command.

#### Configuration options

You can set the following configuration options.

| Option | Description |
| :--- | :--- |
| `installRoot` | Location where YugabyteDB Anywhere is installed. Default is `/opt/yugabyte`. |
| `host` | Hostname or IP Address used for CORS and certificate creation. Optional. |
| `server_cert_path`<br />`server_key_path` | If providing custom certificates, give the path with these values. If not provided, the installation process generates self-signed certificates. Optional. |
| `service_username` | The Linux user that will run the YugabyteDB Anywhere processes. Default is `yugabyte`. The install process will create the `yugabyte` user. If you wish to use a different user, create that user beforehand and specify it in `service_username`. YBA Installer only creates the `yugabyte` user, not custom usernames. |

#### YugabyteDB Anywhere configuration options

| Option | Description |
| :--- | :--- |
| Port | Specify a custom port for the YugabyteDB Anywhere UI to run on
| keyStorePassword | Password for the Java keystore. Will be generated if left empty
| appSecret | Play framework crypto secret. Will be generated if left empty

OAuth related settings are described in the following table.

| Option | Description |
| :--- | :--- |
| `useOauth` | Boolean that determines if OIDC SSO needs to be enabled on YugabyteDB Anywhere. Default to false, but override it to true if you intend on using OIDC SSO for your YugabyteDB Anywhere installation (must be a boolean).
| `ybSecurityType` | The Security Type corresponding to the OIDC SSO for your YugabyteDB Anywhere installation. Only set this field if you intend on using OIDC SSO for your platform installation (otherwise leave it empty).
| `ybOidcClientId` | The Client Id corresponding to the OIDC SSO for your platform installation. Only set this field if you intend on using OIDC SSO for your platform installation (otherwise leave it empty).
| `ybOidcSecret` | The OIDC Secret Key corresponding to the OIDC SSO for your platform installation. Only set this field if you intend on using OIDC SSO for your platform installation (otherwise leave it empty).
| `ybOidcDiscoveryUri` | The OIDC Discovery URI corresponding to the OIDC SSO for your platform installation. Only set this field if you intend on using OIDC SSO for your platform installation (otherwise leave it empty, must be a valid url).
| `ywWrl` | The Platform IP corresponding to the OIDC SSO for your platform installation. Only set this field if you intend on using OIDC SSO for your platform installation (otherwise leave it empty, must be a valid url).
| `ybOidcScope` | The OIDC Scope corresponding to the OIDC SSO for your platform installation. Only set this field if you intend on using OIDC SSO for your platform installation (otherwise leave it empty).
| `ybOidcEmailAtr` | The OIDC Email Attr corresponding to the OIDC SSO for your platform installation. Only set this field if you intend on using OIDC SSO for your platform installation (otherwise leave it empty, must be a valid email address).

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

#### Prometheus configuration options

| Option | Description |
| :--- | :--- |
| `Port` | external Prometheus port
| `restartSeconds` | Systemd will restart Prometheus after this number of seconds after a crash
| `scrapeInterval` | how often Prometheus scrapes for database metrics
| `scrapeTimeout` | timeout for inactivity during scraping
| `maxConcurrency` | Max concurrent queries to be executed by Prometheus
| `maxSamples` | The maximum number of samples that a single query can load into memory
| `Timeout` | The time threshold for inactivity after which Prometheus will be declared inactive

#### Bring your own PostgreSQL

PostgreSQL is divided into two different subsections - `install` and `useExisting`. Install contains information on how YBA Installer should install PostgreSQL, while useExisting is to provide YBA Installer with information on how to connect to a postgres instance that you provision and manage separately. These options are mutually exclusive, and can be turned on/off using the enabled option. Exactly one of these two sections must have enabled = true, while the other must have enabled = false.

**Install options**

| Option | Description |
| :--- | :--- |
| Port | Port PostgreSQL is listening to
| restartSecond | Wait time to restart PostgreSQL if the service crashes
| locale | locale is used during initialization of the db.

**useExisting options**

| Option | Description |
| :--- | :--- |
| Host | IP address/domain name of the PostgreSQL server.
| Port | Port PostgreSQL is running on.
| Username and password | Used to authenticate with PostgreSQL.
| Pg_dump_path<br/>pg_restore_path | Required paths to pgdump and pgrestore on the locale system that are compatible with the version of PostgreSQL you provide. Pgdump and pgrestore are used for backup and restore workflows, and are required for a functioning install.

### Run preflight checks

After yba-ctl is installed and configured, start by running the preflight checks to ensure that the expected ports are available, the hardware meets the YBA minimum requirements, and so forth. The preflight check generates a report you can use to fix any issues before continuing with the installation.

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

Some checks can be ignored, such as CPU or memory. Others, such as python, are hard requirements, and YugabyteDB Anywhere can't work until these checks pass. All checks should pass for a production installation.

If a check is failing and you want to skip it, you can pass `–skip_preflight <name>[,<name2>]`.

```sh
$ sudo ./yba-ctl preflight --skip_preflight cpu
```

At this point, the license check fails as no license has been provided to YBA Installer.

### Provide a license

YBA Installer requires a valid license before installing. To obtain a license, contact {{% support-platform %}}.

The license can be provided to YBA Installer in one of two ways:

- Run the `license` command before running `install`. This command can also be used to update to a new license if needed.

    ```sh
    $ sudo ./yba-ctl license add -l /path/to/license
    ```

- As part of the `install` command:

    ```sh
    $ sudo ./yba-ctl install -l /path/to/license
    ```

After the license is added, all preflight checks should pass:

```sh
$ sudo ./yba-ctl license add -l ~/yugabyte_anywhere.lic
```

```output
INFO[2023-04-18T20:52:13Z] Added license, services can be started now
```

```sh
$ sudo ./yba-ctl preflight
```

```output
#  Check name             Status Error
1  install does not exist Pass
2  validate-config        Pass
3  user                   Pass
4  cpu                    Pass
5  memory                 Pass
6  port                   Pass
7  python                 Pass
8  disk-availability      Pass
9  license                Pass
10 postgres               Pass
```

### Install the software

To perform an install, run the `install` command. Once started, an install can take several minutes to complete.

```sh
$ sudo ./yba-ctl install
```

```output
               YBA Url |   Install Root |            yba-ctl config |              yba-ctl Logs |
  https://10.150.0.218 |  /opt/yugabyte |  /opt/yba-ctl/yba-ctl.yml |  /opt/yba-ctl/yba-ctl.log |

Services:
  Systemd service |       Version |  Port |                            Log File Locations |  Running Status |
         postgres |         10.23 |  5432 |          /opt/yugabyte/data/logs/postgres.log |         Running |
       prometheus |        2.42.0 |  9090 |  /opt/yugabyte/data/prometheus/prometheus.log |         Running |
      yb-platform |  2.19.0.0-b51 |   443 |       /opt/yugabyte/data/logs/application.log |         Running |
INFO[2023-04-24T23:19:59Z] Successfully installed YugabyteDB Anywhere!
```

`install` runs all preflight checks first, and then proceeds to do a full install, and then waits for YugabyteDB Anywhere to start. After the install succeeds, you can immediately start using YugabyteDB Anywhere.

## Reconfigure

YBA Installer can be used to reconfigure an installed YBA instance. Some basics can be changed here, such as the proxy settings for ‘platform’. Others are unable to be changed, such as the install root, service username, or if you brought your own postgres.

To reconfigure an installation, edit the `/opt/yba-ctl/yba-ctl.yml` configuration file with your changes, and then run the command as follows:

```sh
$ sudo yba-ctl reconfigure
```

## Service management

yba-ctl also provides basic service management, with `start`, `stop`, and `restart` commands. Each of these can be performed for all the services (platform, postgres, and prometheus), or any individual service.

```sh
$ sudo yba-ctl [start, stop, reconfigure]
$ sudo yba-ctl [start, stop, reconfigure] prometheus
```

In addition to the state changing operations, you can use the `status` command to show the status of all YBA services, in addition to other information such as the log and configuration location, versions of each service, and the URL to access the YugabyteDB Anywhere UI.

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
      yb-platform |  2.19.0.0-b59 |   443 |       /opt/yugabyte/data/logs/application.log |         Running |
```

## Upgrade

To upgrade using YBA Installer, first download the version of YBA Installer corresponding to the version of YugabyteDB Anywhere you want to upgrade to. See [Download YBA Installer](#download-yba-installer). Upgrade works similarly to the install workflow, by first running preflight checks to validate the system is in a good state. When ready to upgrade, run the upgrade command from the untarred directory of the target version of the YBA upgrade:

```sh
$ sudo ./yba-ctl upgrade
```

The upgrade takes a few minutes to complete. When finished, use the `status` command to verify that YugabyteDB Anywhere has been upgraded to the target version.

## Backup and restore

YBA Installer also provides utilities to take full backups of the YugabyteDB Anywhere state (not YugabyteDB however) and later restore from them. This not only includes data seen in YBA for your universes, but also metrics stored in Prometheus.

To perform a backup, provide the full path to the directory where the backup will be generated. The `createBackup` command creates a timestamped `tgz` file for the backup. For example:

```sh
$ sudo yba-ctl createBackup ~/test_backup
$ ls test_backup/
```

```output
backup_23-04-25-16-54.tgz  version_metadata_backup.json
```

To restore from the same backup, use the `restoreBackup` commmand:

```sh
$ sudo yba-ctl restoreBackup ~/test_backup/backup_23-04-25-16-64.tgz
```

## Clean (uninstall)

To uninstall a YBA instance, YBA Installer also provides a clean functionality with two modes. By default, `clean` removes the YugabyteDB Anywhere software, but keeps any data such as PostgreSQL or Prometheus information. You can also run `clean` with the `–all` flag to delete all data.

```sh
$ sudo yba-ctl clean
```

```output
INFO[2023-04-24T23:58:13Z] Uninstalling yb-platform
INFO[2023-04-24T23:58:14Z] Uninstalling prometheus
INFO[2023-04-24T23:58:14Z] Uninstalling postgres
```

```sh
$ sudo yba-ctl clean -all
```

```output
--all was specified. This will delete all data with no way to recover. Continue? [yes/NO]: y
INFO[2023-04-24T23:58:13Z] Uninstalling yb-platform
INFO[2023-04-24T23:58:14Z] Uninstalling prometheus
INFO[2023-04-24T23:58:14Z] Uninstalling postgres
```

## Yugabundle migration

Detailed instructions on how to migrate from a Yugabundle deployment to a YBA-Installer deployment.

### Migrate from Yugabundle to YBA Installer

Binary Execution Path

The above workflow descriptions use 2 different methods to execute yba-ctl. The first seen is local execution using ./yba-ctl, and the second is using just yba-ctl. This is done on purpose, as some commands require local execution context.

- such as install or upgrade - while others require the context of the installed system, such as the backup commands or clean. Some commands do not have this context, such as license or preflight, as these can be run on both installed systems and fresh environments

If the correct execution is not used, yba-ctl will fail with errors

```sh
$ sudo ./yba-ctl createBackup ~/backup.tgz
```

```output
FATAL[2023-04-25T00:14:57Z] createBackup must be run from the installed yba-ctl
```

Local-only Commands:

- install
- upgrade

Global-only Commands:

- createBackup
- restoreBackup
- clean
- start, stop, restart, and status
- reconfigure
- Both
- preflight
- license

## Non-root installation

YBA Installer also supports a non-root installation, where sudo access is not required for any step of the installation. Note that this is not recommended for production use cases.

To facilitate a non-root install, YBA Installer will not create any additional users or set up services in systemd. The install will also be rooted in the home directory by default, instead of /opt, ensuring YBA Installer has write access to the base install directory. Instead of using systemd to manage services, basic cron jobs are used to start the services on bootup with basic management scripts used to restart the services after a crash.

To perform a non-root install, run any of the above commands without root access. You can't switch between a root and non-root install, and yba-ctl will return an error if sudo is not used when operating in a root install.
