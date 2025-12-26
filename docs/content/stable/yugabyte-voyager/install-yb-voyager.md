---
title: Install yb-voyager
headerTitle: Install
linkTitle: Install
description: Prerequisites and installation instructions for YugabyteDB Voyager.
menu:
  stable_yugabyte-voyager:
    identifier: install-yb-voyager
    parent: yugabytedb-voyager
    weight: 101
type: docs
---

## Prerequisites

The following sections describe the prerequisites for installing YugabyteDB Voyager.

### Operating system

You can install YugabyteDB Voyager on the following:

- RHEL 8, 9
- CentOS 8
- Ubuntu 18.04, 20.04, 22.04
- macOS (for MySQL/Oracle source databases on macOS, [install yb-voyager](#install-yb-voyager) using the Docker option.)

### Hardware requirements

- Disk space of at least 2 times the estimated size of the source database
- 2 cores minimum (recommended)

### Software requirement

- Java 17. Any higher versions of Java might lead to errors during installation or migration.

### Prepare the host

The node where you'll run the yb-voyager command should:

- connect to both the source and the target database.
- have sudo access.

## Install yb-voyager

YugabyteDB Voyager consists of the yb-voyager command line executable.

Install yb-voyager on a machine which satisfies the [Prerequisites](#prerequisites) using one of the following options:

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#rhel" class="nav-link active" id="rhel-tab" data-bs-toggle="tab" role="tab" aria-controls="rhel" aria-selected="true">
      <i class="fa-brands fa-redhat" aria-hidden="true"></i>
      RHEL
    </a>
  </li>
  <li>
    <a href="#ubuntu" class="nav-link" id="ubuntu-tab" data-bs-toggle="tab" role="tab" aria-controls="ubuntu" aria-selected="true">
      <i class="fa-brands fa-ubuntu" aria-hidden="true"></i>
      Ubuntu
    </a>
  </li>
    <li >
    <a href="#macos" class="nav-link" id="macos-tab" data-bs-toggle="tab" role="tab" aria-controls="macos" aria-selected="true">
      <i class="fa-brands fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="#airgapped" class="nav-link" id="airgapped-tab" data-bs-toggle="tab" role="tab" aria-controls="airgapped" aria-selected="true">
      <i class="fa-solid fa-link-slash" aria-hidden="true"></i>
      Airgapped
    </a>
  </li>
  <li>
    <a href="#docker" class="nav-link" id="docker-tab" data-bs-toggle="tab" role="tab" aria-controls="docker" aria-selected="true">
      <i class="fa-brands fa-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>
  <li>
    <a href="#github" class="nav-link" id="github-tab" data-bs-toggle="tab" role="tab" aria-controls="github" aria-selected="true">
      <i class="fab fa-github" aria-hidden="true"></i>
      Source
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="rhel" class="tab-pane fade show active" role="tabpanel" aria-labelledby="rhel-tab">
{{% readfile "./rhel.md" %}}
  </div>
  <div id="ubuntu" class="tab-pane fade" role="tabpanel" aria-labelledby="ubuntu-tab">
{{% readfile "./ubuntu.md" %}}
  </div>
  <div id="macos" class="tab-pane fade" role="tabpanel" aria-labelledby="macos-tab">
{{% readfile "./macos.md" %}}
  </div>
  <div id="airgapped" class="tab-pane fade" role="tabpanel" aria-labelledby="airgapped-tab">

{{< tabpane text=true >}}

{{% tab header="Docker" lang="docker" %}}

You can perform an airgapped installation on Docker.

Install yb-voyager using a Docker image in an airgapped environment using the following steps:

1. From a machine connected to the internet, run the following commands to pull and save the latest yb-voyager docker image (Pull the version from docker.io):

    ```sh
    docker pull yugabytedb/yb-voyager
    docker save -o yb-voyager-image.tar yugabytedb/yb-voyager:latest
    gzip yb-voyager-image.tar
    ```

1. Download the yb-voyager wrapper script on the same machine using the following command:

    ```sh
    wget -O ./yb-voyager https://software.yugabyte.com/yugabyte/yb-voyager/main/docker/yb-voyager-docker
    ```

1. Copy the `yb-voyager-image.tar.gz` and `yb-voyager` files to the airgapped machine.

1. Load the docker image using the following command:

    ```sh
    gunzip yb-voyager-image.tar.gz
    docker load --input yb-voyager-image.tar
    ```

1. Make the wrapper script executable and move it to the `bin` directory using the following commands:

    ```sh
    chmod +x yb-voyager
    sudo mv yb-voyager /usr/local/bin
    ```

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```

{{% /tab %}}

{{% tab header="Yum" lang="yum" %}}

You can perform an airgapped installation on RHEL 8/9 and CentOS 8/9.

1. Download the airgapped bundle:

    - For RHEL8:

        ```sh
        wget https://software.yugabyte.com/repos/airgapped/yb-voyager-latest-rhel-8-x86_64.tar.gz
        ```

    - For RHEL9:

        ```sh
        wget https://software.yugabyte.com/repos/airgapped/yb-voyager-latest-rhel-9-x86_64.tar.gz
        ```

1. Extract the bundle.

    ```sh
    tar -xvf <tar-bundle-name>
    ```

    It contains three packages - debezium, ora2pg, and yb-voyager.

1. Download the airgapped installation script into the extracted bundle directory:

    ```sh
    wget -P </path/to/directory> raw.githubusercontent.com/yugabyte/yb-voyager/main/installer_scripts/install-voyager-airgapped.sh
    ```

1. Make the script executable:

    ```sh
    chmod +x /path/to/directory/install-voyager-airgapped.sh
    ```

1. Transfer the folder (which contains the 3 packages and the installer script) to the airgapped machine.
1. Install all the [dependencies](#dependencies-for-rhel-and-centos-8) on the airgapped machine.
1. Run the [installer script](#installation-script) on the airgapped machine to check the dependencies and install voyager:

    ```sh
    ./install-voyager-airgapped.sh
    ```

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```

### Dependencies for RHEL 8/9 and CentOS 8/9

Binutils: Minimum version: 2.25

Java: Minimum version: 17

pg_dump: Minimum version: 14

pg_restore: Minimum version: 14

psql: Minimum version: 14

#### Yum packages

- make (no version dependency)
- sqlite (no version dependency)
- perl (no version dependency)
- perl-DBI (no version dependency)
- perl-App-cpanminus (no version dependency)
- perl-ExtUtils-MakeMaker (no version dependency)
- mysql-devel (For RHEL 8) (no version dependency)
- mysql-community-devel (For RHEL 9) (no version dependency)
- oracle-instantclient-tools with exact version 21.5.0.0.0
- oracle-instantclient-basic with exact version 21.5.0.0.0
- oracle-instantclient-devel with exact version 21.5.0.0.0
- oracle-instantclient-jdbc with exact version 21.5.0.0.0
- oracle-instantclient-sqlplus with exact version 21.5.0.0.0

### Installation Script

The script by default checks what dependencies are installed on the system and throws an error mentioning the missing dependencies. If all the dependencies are found to be installed, it proceeds with the installation of ora2pg, debezium, and yb-voyager.

Usage:

```sh
./install-voyager-airgapped.sh [options]
```

The options are as follows.

| Argument                       | Description/valid options                                                     |
| :----------------------------- | :--------------------------------------------------------------------------- |
| -d, --check-dependencies-only | Check the dependencies only, then exit without installing.                                           |
| -f, --force-install           | Force install packages without checking dependencies.                       |
| -p, --pg-only                 | Check and install only PostgreSQL source-related voyager dependencies.     |
| -m, --mysql-only              | Check and install only MySQL source-related voyager dependencies.          |
| -o, --oracle-only             | Check and install only Oracle source-related voyager dependencies.         |
| -h, --help                    | Display this help message.                                                 |

You can only specify one of `--pg-only`, `--oracle-only`, or `--mysql-only`. If none are provided, the script checks and installs dependencies for all database types. When one of the flags is specified, `--help` and `--check-dependencies-only` are specific to the selected database.

### Oracle Instant Client installation help for Centos/RHEL

You can download the oracle instant client rpms from the following links:

- [oracle-instantclient-tools](https://download.oracle.com/otn_software/linux/instantclient/215000/oracle-instantclient-tools-21.5.0.0.0-1.x86_64.rpm)

- [oracle-instantclient-basic](https://download.oracle.com/otn_software/linux/instantclient/215000/oracle-instantclient-basic-21.5.0.0.0-1.x86_64.rpm)

- [oracle-instantclient-devel](https://download.oracle.com/otn_software/linux/instantclient/215000/oracle-instantclient-devel-21.5.0.0.0-1.x86_64.rpm)

- [oracle-instantclient-jdbc](https://download.oracle.com/otn_software/linux/instantclient/215000/oracle-instantclient-jdbc-21.5.0.0.0-1.x86_64.rpm)

- [oracle-instantclient-sqlplus](https://download.oracle.com/otn_software/linux/instantclient/215000/oracle-instantclient-sqlplus-21.5.0.0.0-1.x86_64.rpm)

{{% /tab %}}

{{% tab header="Ubuntu" lang="ubuntu" %}}

You can perform an airgapped installation on Ubuntu 22 and later.

1. Download the airgapped bundle:

    ```sh
    wget https://software.yugabyte.com/repos/airgapped/yb-voyager-latest_debian.tar.gz
    ```

1. Extract the bundle.

    ```sh
    tar -xvf <tar-bundle-name>
    ```

    It contains three packages - debezium, ora2pg, and yb-voyager.

1. Download the airgapped installation script into the extracted bundle directory:

    ```sh
    wget -P </path/to/directory> raw.githubusercontent.com/yugabyte/yb-voyager/main/installer_scripts/install-voyager-airgapped.sh
    ```

1. Make the script executable:

    ```sh
    chmod +x /path/to/directory/install-voyager-airgapped.sh
    ```

1. Transfer the folder (which contains the 3 packages and the installer script) to the airgapped machine.
1. Install all the [dependencies](#dependencies-for-ubuntu) on the airgapped machine.
1. Run the [install script](#install-script) on the airgapped machine to check the dependencies and install voyager:

    ```sh
    ./install-voyager-airgapped.sh
    ```

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```

### Dependencies for Ubuntu

Binutils: Minimum version: 2.25

Java: Minimum version: 17

pg_dump: Minimum version: 14

pg_restore: Minimum version: 14

psql: Minimum version: 14

#### APT packages

- sqlite3 (no version dependency)
- make (no version dependency)
- perl (no version dependency)
- libdbi-perl (no version dependency)
- libaio1 (no version dependency)
- cpanminus (no version dependency)
- libmysqlclient-dev (no version dependency)
- oracle-instantclient-tools with exact version 21.5.0.0.0
- oracle-instantclient-basic with exact version 21.5.0.0.0
- oracle-instantclient-devel with exact version 21.5.0.0.0
- oracle-instantclient-jdbc with exact version 21.5.0.0.0
- oracle-instantclient-sqlplus with exact version 21.5.0.0.0

### Install script

The script by default checks what dependencies are installed on the system and throws an error mentioning the missing dependencies. If all the dependencies are found to be installed, it proceeds with the installation of ora2pg, debezium, and yb-voyager.

Usage:

```sh
./install-voyager-airgapped.sh [options]
```

The options are as follows.

| Argument                       | Description/valid options                                                     |
| :----------------------------- | :--------------------------------------------------------------------------- |
| -d, --check-dependencies-only | Check the dependencies only, then exit without installing.                                           |
| -f, --force-install           | Force install packages without checking dependencies.                       |
| -p, --pg-only                 | Check and install only PostgreSQL source-related voyager dependencies.     |
| -m, --mysql-only              | Check and install only MySQL source-related voyager dependencies.          |
| -o, --oracle-only             | Check and install only Oracle source-related voyager dependencies.         |
| -h, --help                    | Display this help message.                                                 |

You can only specify one of `--pg-only`, `--oracle-only`, or `--mysql-only`. If none are provided, the script checks and installs dependencies for all database types. When one of the flags is specified, `--help` and `--check-dependencies-only` are specific to the selected database.

### Oracle Instant Client installation help for Ubuntu

You can download the oracle instant client RPM packages from the following links:

- [oracle-instantclient-tools](https://downloads.yugabyte.com/repos/apt/pool/main/oracle-instantclient-tools_21.5.0.0.0-1_amd64.deb)

- [oracle-instantclient-basic](https://downloads.yugabyte.com/repos/apt/pool/main/oracle-instantclient-basic_21.5.0.0.0-1_amd64.deb)

- [oracle-instantclient-devel](https://downloads.yugabyte.com/repos/apt/pool/main/oracle-instantclient-devel_21.5.0.0.0-1_amd64.deb)

- [oracle-instantclient-jdbc](https://downloads.yugabyte.com/repos/apt/pool/main/oracle-instantclient-jdbc_21.5.0.0.0-1_amd64.deb)

- [oracle-instantclient-sqlplus](https://downloads.yugabyte.com/repos/apt/pool/main/oracle-instantclient-sqlplus_21.5.0.0.0-1_amd64.deb)

{{% /tab %}}

{{< /tabpane >}}

  </div>
  <div id="docker" class="tab-pane fade" role="tabpanel" aria-labelledby="docker-tab">
{{% readfile "./docker.md" %}}
  </div>
  <div id="github" class="tab-pane fade" role="tabpanel" aria-labelledby="github-tab">
{{% readfile "./github.md" %}}
  </div>
</div>

## Collect diagnostics

By default, yb-voyager captures a [diagnostics report](../reference/diagnostics-report/) using the YugabyteDB diagnostics service that runs each time you use the yb-voyager command. If you don't want to send diagnostics when you run yb-voyager, set the [--send-diagnostics flag](../reference/diagnostics-report/#configure-diagnostics-collection) to false.

## Use configuration files

For convenience, you can define all the parameters required for running yb-voyager commands using a configuration file (v2025.6.2 or later). Configuration files are in YAML format, and templates are available. For more information, refer to [Configuration file](../reference/configuration-file).

## Next step

- [Migrate](../migrate/)
