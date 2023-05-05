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

Installing YugabyteDB Anywhere using the Installer involves the following:

- [Installing the Operator itself](#install-the-operator)
- [Creating an instance of YugabyteDB Anywhere](#create-an-instance-of-yugabyte-platform-via-operator)
- [Finding the availability zone labels](#find-the-availability-zone-labels)
- [Configuring the CLI with the OCP cluster](#configure-the-cli-with-the-ocp-cluster)
- [Accessing and configuring YugabyteDB Anywhere](#access-and-configure-yugabyte-platform)
- Optionally, [upgrading the YugabyteDB Anywhere instance](#upgrade-the-yugabyte-platform-instance)

### Download the Installer

To get YBA Installer, first download a YBA_installer_full.tgz bundle and extract it:

```sh
$ wget YBA_installer_url/YBA_installer_full-2.17.3.0-b123-centos-x86_64.tar.gz
$ tar -xf YBA_installer_full-2.17.3.0-b123-centos-x86_64.tar.gz

$ ls 
YBA_installer_full-2.17.3.0-b123  YBA_installer_full-2.17.3.0-b123-centos-x86_64.tar.gz

$ cd YBA_installer_full-2.17.3.0-b123/
$ ls
version_metadata.json  yba-ctl  yba-ctl.yml.reference  YBA_installer-2.17.3.0-b123-linux-amd64  yugabundle-2.17.4.0-b123-centos-x86_64.tar.gz
```

This bundle provides everything needed, except a license, to complete a fresh install of YugabyteDB Anywhere.

- `yba-ctl` executable binary will be used to perform all of the YBA Installer workflows
- `yba-ctl.yml.reference` is a YAML reference to available config options both YBA Installer and YBA itself. Some of these config options cannot be changed after installation.

To see a full list of commands, run the following command:

```sh
$ ./yba-ctl help
```

## Config Setup

Many commands provided by YBA Installer require a config file - commands like preflight and install. If these commands are used, there will be a prompt around continuing with the default config values:

```sh
$ sudo ./yba-ctl preflight
No config file found at '/opt/yba-ctl/yba-ctl.yml', creating it with default values now.
Do you want to proceed with the default config? [yes/NO]:
```

If answered with ‘y’ or ‘yes’, the work continues with the default config values. One can view this at the given location (/opt/yba-ctl/yba-ctl.yml in this case). If answered with ‘n’ or ‘no’ is given, this file is still created, but the command exits. To modify the default values, proceed to edit the file, and then re-run the yba-ctl command.

### Config options

installRoot: Where the YBA install will be located at. Default is /opt/yugabyte
host: Hostname or IP Address that will be used for CORS and certificate creation. Optional

Server_cert_path and server_key_path: if providing custom certificates, give the path with these values. If left empty, the install process will generate self-signed certificates. Optional

service_username: the linux user that will run the YBA processes. Default to yugabyte

NOTE: The install process will create the yugabyte user. If you wish to use a different user, please create that user beforehand and specify it in service_username. YBA Installer will only create the yugabyte user, not custom usernames.

Platform: Platform config options are located here 
Port: specify a custom port the platform webpage will run on

OAuth related settings
 useOauth: The boolean that determine if OIDC SSO needs to be enabled on the Platform. Default to false, but override it to true if you intend on using OIDC SSO for your platform installation (must be a boolean).

ybSecurityType: The Security Type corresponding to the OIDC SSO for your platform installation. Only set this field if you intend on using OIDC SSO for your platform installation (otherwise leave it empty).

ybOidcClientId: The Client Id corresponding to the OIDC SSO for your platform installation. Only set this field if you intend on using OIDC SSO for your platform installation (otherwise leave it empty).

ybOidcSecret: The OIDC Secret Key corresponding to the OIDC SSO for your platform installation. Only set this field if you intend on using OIDC SSO for your platform installation (otherwise leave it empty).

ybOidcDiscoveryUri: The OIDC Discovery URI corresponding to the OIDC SSO for your platform installation. Only set this field if you intend on using OIDC SSO for your platform installation (otherwise leave it empty, must be a valid url).

ywWrl: The Platform IP corresponding to the OIDC SSO for your platform installation. Only set this field if you intend on using OIDC SSO for your platform installation (otherwise leave it empty, must be a valid url).

ybOidcScope: The OIDC Scope corresponding to the OIDC SSO for your platform installation. Only set this field if you intend on using OIDC SSO for your platform installation (otherwise leave it empty).

ybOidcEmailAtr: The OIDC Email Attr corresponding to the OIDC SSO for your platform installation. Only set this field if you intend on using OIDC SSO for your platform installation (otherwise leave it empty, must be a valid email address).

keyStorePassword: password fo java keystore. Will be generated if left empty
appSecret: play framework crypto secret. Will be generated if left empty
Http and Https proxy settings
http_proxy:            Specify the setting for HTTP_PROXY
java_http_proxy_port:  Specify -Dhttp.proxyPort
java_http_proxy_host:  Specify -Dhttp.proxyHost
https_proxy:           Specify the setting for HTTPS_PROXY
java_https_proxy_port: Specify -Dhttps.proxyPort
java_https_proxy_host: Specify -Dhttps.proxyHost
no_proxy:              Specify the setting for NO_PROXY
java_non_proxy:        Specify  -Dhttps.nonProxyHosts.

postgres: Postgres config options are here. If you do not wish to use the postgres version provided by YBA Installer, please ensure this is filled out correctly
See Bring Your Own Postgres

prometheus: Prometheus config values here
Port: external prometheus port
restartSeconds: Systemd will restart prometheus after this number of seconds after a crash

scrapeInterval: how often prometheus scrapes for database metrics

scrapeTimeout: timeout for inactivity during scraping

maxConcurrency: Max concurrent queries to be executed by prometheus

maxSamples: The maximum number of samples that a single query can load into memory

Timeout: The time threshold for inactivity after which prometheus will be declared inactive

### Bring Your Own Postgres

Postgres is divided into two different subsections - “install” and “useExisting”. Install contains information on how YBA Installer should install Postgres, while useExisting is to provide YBA Installer with information on how to connect to a postgres instance that you provision and manage separately. These two sections are mutually exclusive, and can be turned on/off using the “enabled” option. Exactly one of these two sections must have enabled = true, while the other must have enabled = false.

Install Options:

Port: port Postgres is listening to

restartSecond:wait time to restart postgres if the service crashes

locale: locale is used during initialization of the db.

useExisting

Host: ip address/domain name of the Postgres server
Port: port postgres is running on. 
Username and password: used to authenticate with postgres
Pg_dump_path pg_restore_path. 
Required paths to pgdump and pgrestore on the locale system that are compatible with the version of postgres you provide. Pgdump and pgrestore are used for backup and restore workflows, and are required for a functioning install

### Preflight Checks

Once we have access to yba-ctl, we can start by running the preflight checks provided by YBA Installer. This will run checks to ensure that our expected ports are available, the hardware meets the YBA minimum requirements, as well as a few others. All preflight checks will be run and a report generated, even if some fail. This will allow you to fix all found issues before continuing with the installation.

```sh
$ sudo ./yba-ctl preflight
…
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

Some of these checks can be skipped - such as cpu or memory. Others, such as python, are hard requirements and YBA will not work until these checks pass. It is recommended that all checks pass for a production installation.

If a check is failing and you want to skip it, you can pass –skip_preflight <name>[,<name2>]. Please  note not all checks are skippable.

```sh
$ sudo ./yba-ctl preflight --skip_preflight cpu
```

At this point, the license check is failing as we have not yet provided our license to YBA Installer.

### License

YBA Installer requires a valid license before installing. To obtain a license, please work with your Yugabyte contact.

The license can be provided to YBA Installer in one of two ways:

Stand alone command before running install. This can also be used to update to a new license if needed.

```sh
$ sudo ./yba-ctl license add -l /path/to/license
```

With the install command

```sh
$ sudo ./yba-ctl install -l /path/to/license
```

Now that a license is added, all preflight checks should pass:

```sh
$ sudo ./yba-ctl license add -l ~/yugabyte_anywhere.lic
INFO[2023-04-18T20:52:13Z] Added license, services can be started now
```

```sh
$ sudo ./yba-ctl preflight
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

### Install

We can now perform an install. Please note, that once started, an install can take several minutes to complete

```sh
$ sudo ./yba-ctl install
…
               YBA Url |   Install Root |            yba-ctl config |              yba-ctl Logs |
  https://10.150.0.218 |  /opt/yugabyte |  /opt/yba-ctl/yba-ctl.yml |  /opt/yba-ctl/yba-ctl.log |

Services:
  Systemd service |       Version |  Port |                            Log File Locations |  Running Status |
         postgres |         10.23 |  5432 |          /opt/yugabyte/data/logs/postgres.log |         Running |
       prometheus |        2.42.0 |  9090 |  /opt/yugabyte/data/prometheus/prometheus.log |         Running |
      yb-platform |  2.19.0.0-b51 |   443 |       /opt/yugabyte/data/logs/application.log |         Running |
INFO[2023-04-24T23:19:59Z] Successfully installed YugabyteDB Anywhere!
```

The install will run all preflight checks first, and then proceed to do a full install and wait for YugabyteDB Anywhere to start. Once the install succeeds, you can immediately begin using it to deploy and manage YugabyteDB Universes.

## Reconfigure

YBA Installer can be used to reconfigure an installed YBA instance. Some basics can be changed here, such as the proxy settings for ‘platform’. Others are unable to be changed, such as the install root, service username, or if you brought your own postgres.

To reconfigure, edit /opt/yba-ctl/yba-ctl.yml with your changes, and then run

```sh
$ sudo yba-ctl reconfigure
```

### Service Management

Basic service management can also be done through yba-ctl - start, stop, and restart are currently supported. Each of these can be performed for all the services (platform, postgres, and prometheus), or any individual service

```sh
$ sudo yba-ctl [start, stop, reconfigure]
$ sudo yba-ctl [start, stop, reconfigure] prometheus
```

In addition to these state changing operations, a status command is also provided. This will show the status of all the YBA services, in addition to other information like the log and config location, versions of each service, and the URL to access YBA.

```sh
$ sudo yba-ctl status
               YBA Url |   Install Root |            yba-ctl config |              yba-ctl Logs |
  https://10.150.0.218 |  /opt/yugabyte |  /opt/yba-ctl/yba-ctl.yml |  /opt/yba-ctl/yba-ctl.log |

Services:
  Systemd service |       Version |  Port |                            Log File Locations |  Running Status |
         postgres |         10.23 |  5432 |          /opt/yugabyte/data/logs/postgres.log |         Running |
       prometheus |        2.42.0 |  9090 |  /opt/yugabyte/data/prometheus/prometheus.log |         Running |
      yb-platform |  2.19.0.0-b59 |   443 |       /opt/yugabyte/data/logs/application.log |         Running |
```

## Upgrade

To upgrade using YBA Installer, one must first download the YBA_installer_full tar.gz file of the version of YBA to upgrade to. See Access YBA Installer for full details. Upgrade will work very similar to the install workflow, running preflight checks first to validate the system is in a good state. When ready to upgrade, run the upgrade command from the untarred directory of the target version of the YBA upgrade:

```sh
$ sudo ./yba-ctl upgrade
```

This will also take a few minutes to complete, and once it does you can see, via the status command, that the yb-platform version is now the new upgrade target version.

## Backup and Restore

Yba Installer also provides utilities to take full backups of the YBA state - not YBDB - and later restore from them. This not only includes data seen in YBA for your universes, but also metrics stored in prometheus. To take a backup into some backup.tgz, a full path to the directory where the backup will be generated is required. Under this directory, createBackup will create a timestamped tgz file for the backup. For example:

```sh
$ sudo yba-ctl createBackup ~/test_backup
$ ls test_backup/
backup_23-04-25-16-54.tgz  version_metadata_backup.json
```

To later restore from the same backup:

```sh
$ sudo yba-ctl restoreBackup ~/test_backup/backup_23-04-25-16-64.tgz
```

## Clean (uninstall)

If you wish to uninstall the YBA instance, YBA Installer also provides a “clean” functionality. It has two modes. By default, it will remove all “software”, but keeps any data - such as postgres or prometheus information. One can also run clean with –all to delete all data.

```sh
$ sudo yba-ctl clean
INFO[2023-04-24T23:58:13Z] Uninstalling yb-platform
INFO[2023-04-24T23:58:14Z] Uninstalling prometheus
INFO[2023-04-24T23:58:14Z] Uninstalling postgres
```

```sh
$ sudo yba-ctl clean -all
--all was specified. This will delete all data with no way to recover. Continue? [yes/NO]: y
INFO[2023-04-24T23:58:13Z] Uninstalling yb-platform
INFO[2023-04-24T23:58:14Z] Uninstalling prometheus
INFO[2023-04-24T23:58:14Z] Uninstalling postgres
```

## Yugabundle Migration

Detailed instructions on how to migrate from a Yugabundle deployment to a YBA-Installer deployment.

### Yugabundle to YBA-installer Migration

Binary Execution Path

The above workflow descriptions use 2 different methods to execute yba-ctl. The first seen is local execution using ./yba-ctl, and the second is using just yba-ctl. This is done on purpose, as some commands require local execution context 

- such as install or upgrade - while others require the context of the installed system, such as the backup commands or clean. Some commands do not have this context, such as license or preflight, as these can be run on both installed systems and fresh environments

If the correct execution is not used, yba-ctl will fail with errors

```sh
$ sudo ./yba-ctl createBackup ~/backup.tgz
FATAL[2023-04-25T00:14:57Z] createBackup must be run from the installed yba-ctl
```

Local Only Commands
Install
Upgrade

Global Only Commands
createBackup
restoreBackup
Clean
Start, stop, restart, and status
Reconfigure 
Both
Preflight
License

## Non-Root Install

YBA Installer also supports a non-root install, where sudo access is not required for any step of the installation. Please note, this is not recommended for production use cases. To facilitate a non-root install, YBA Installer will not create any additional users or setup services in systemd. The install will also be rooted in the home directory by default, instead of /opt, ensuring yba-installer has write access to the base install directory. Instead of using systemd to manage services, basic cron jobs will be used to start the services on bootup with basic management scripts used to restart the services after a crash.

To perform a non-root install, run any of the above commands without root access. It is not permitted to change between a root and non-root install, and yba-ctl will error if sudo is not used when operating in a root install.
