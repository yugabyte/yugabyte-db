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

You can perform an airgapped installation of YugabyteDB Voyager on RHEL 8/9 and CentOS 8/9.

### Prepare the installation bundle

1. Download the airgapped bundle:

    For RHEL 8:

    ```sh
    wget https://software.yugabyte.com/repos/airgapped/yb-voyager-latest-rhel-8-x86_64.tar.gz
    ```

    For RHEL 9:

    ```sh
    wget https://software.yugabyte.com/repos/airgapped/yb-voyager-latest-rhel-9-x86_64.tar.gz
    ```

1. Extract the bundle:

    ```sh
    tar -xvf <tar-bundle-name>
    ```

    The extracted bundle contains the following packages:

    | Package | Version |
    | :------ | :------ |
    | YugabyteDB Voyager | \<voyager_version\> |
    | Debezium | 2.5.2-\<voyager_version\> |
    | Ora2pg | 23.2-yb.2 |

1. Download the airgapped installation script into the extracted bundle directory:

    ```sh
    wget -P </path/to/directory> https://raw.githubusercontent.com/yugabyte/yb-voyager/main/installer_scripts/install-voyager-airgapped.sh
    ```

1. Make the script executable:

    ```sh
    chmod +x /path/to/directory/install-voyager-airgapped.sh
    ```

1. Transfer the folder (containing the packages and installer script) to the airgapped machine.

### Install dependencies

Install the following dependencies on the airgapped machine _before_ running the installer script. If any dependency is missing, the script exits and lists the missing packages.

**System tools**

| Dependency | Version / Constraint |
| :--------- | :------------------- |
| binutils | ≥ 2.25 |
| java | ≥ 17 |
| make | – |
| sqlite | – |
| perl | – |
| perl-DBI | – |
| perl-App-cpanminus | – |
| perl-ExtUtils-MakeMaker | – |

**PostgreSQL client tools**

Install the following PostgreSQL 17 client tools and make it available in your system PATH.

| Dependency | Required Version |
| :--------- | :--------------- |
| pg_dump | 17 |
| pg_restore | 17 |
| psql | 17 |

**MySQL development libraries**

| OS Version | Dependency |
| :--------- | :---------- |
| RHEL 8 / CentOS 8 | mysql-devel |
| RHEL 9 / CentOS 9 | mysql-community-devel |

**Oracle Instant Client (required versions)**

Install the exact version of all Oracle Instant Client packages as follows:

| Dependency | Required Version |
| :--------- | :--------------- |
| [oracle-instantclient-basic](https://download.oracle.com/otn_software/linux/instantclient/215000/oracle-instantclient-basic-21.5.0.0.0-1.x86_64.rpm) | 21.5.0.0.0 |
| [oracle-instantclient-devel](https://download.oracle.com/otn_software/linux/instantclient/215000/oracle-instantclient-devel-21.5.0.0.0-1.x86_64.rpm) | 21.5.0.0.0 |
| [oracle-instantclient-jdbc](https://download.oracle.com/otn_software/linux/instantclient/215000/oracle-instantclient-jdbc-21.5.0.0.0-1.x86_64.rpm) | 21.5.0.0.0 |
| [oracle-instantclient-sqlplus](https://download.oracle.com/otn_software/linux/instantclient/215000/oracle-instantclient-sqlplus-21.5.0.0.0-1.x86_64.rpm) | 21.5.0.0.0 |
| [oracle-instantclient-tools](https://download.oracle.com/otn_software/linux/instantclient/215000/oracle-instantclient-tools-21.5.0.0.0-1.x86_64.rpm) | 21.5.0.0.0 |

### Run the installer script

1. After installing all the dependencies, run the installer script on the airgapped machine:

    ```sh
    ./install-voyager-airgapped.sh
    ```

    The script:

    - Checks whether all required dependencies are installed.
    - Reports any missing dependencies and exits if any are not found.
    - Installs Ora2pg, Debezium, and YugabyteDB Voyager if all checks pass.

    **Script usage**

    ```sh
    ./install-voyager-airgapped.sh [options]
    ```

    | Argument | Description |
    | :------- | :---------- |
    | -d, --check-dependencies-only | Check dependencies only and exit without installing |
    | -f, --force-install | Install packages without checking dependencies |
    | -p, --pg-only | Check/install only PostgreSQL-related dependencies |
    | -m, --mysql-only | Check/install only MySQL-related dependencies |
    | -o, --oracle-only | Check/install only Oracle-related dependencies |
    | -h, --help | Display help message |

    Only one of `--pg-only`, `--mysql-only`, or `--oracle-only` can be specified at a time. If none are provided, the script checks dependencies for all database types.

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```

   The components installed by the script are described in the following table:

    | Component | Version |
    | :-------- | :------ |
    | YugabyteDB Voyager | \<voyager_version\> |
    | Debezium | 2.5.2-\<voyager_version\> |
    | Ora2pg | 23.2-yb.2 |

{{% /tab %}}

{{% tab header="Ubuntu" lang="ubuntu" %}}

You can perform an airgapped installation of YugabyteDB Voyager on Ubuntu 22.04 and later.

### Prepare the installation bundle

1. Download the airgapped bundle:

    ```sh
    wget https://software.yugabyte.com/repos/airgapped/yb-voyager-latest_debian.tar.gz
    ```

1. Extract the bundle:

    ```sh
    tar -xvf <tar-bundle-name>
    ```

    The extracted bundle contains the following packages:

    | Package | Version |
    | :------ | :------ |
    | YugabyteDB Voyager | \<voyager_version\> |
    | Debezium | 2.5.2-\<voyager_version\> |
    | Ora2pg | 23.2-yb.2 |

1. Download the airgapped installation script into the extracted bundle directory:

    ```sh
    wget -P </path/to/directory> https://raw.githubusercontent.com/yugabyte/yb-voyager/main/installer_scripts/install-voyager-airgapped.sh
    ```

1. Make the script executable:

    ```sh
    chmod +x /path/to/directory/install-voyager-airgapped.sh
    ```

1. Transfer the folder (containing the packages and installer script) to the airgapped machine.

### Install dependencies

Install the following dependencies on the airgapped machine _before_ running the installer script. If any dependency is missing, the script exits and reports the missing packages.

**System tools**

| Dependency | Version / Constraint |
| :--------- | :------------------- |
| binutils | ≥ 2.25 |
| java | ≥ 17 |
| make | – |
| sqlite3 | – |
| perl | – |
| libdbi-perl | – |
| cpanminus | – |
| libaio1 | – |

**PostgreSQL client tools**

Install the following PostgreSQL 17 client tools and make it available in your system PATH.

| Dependency | Required Version |
| :--------- | :--------------- |
| pg_dump | 17 |
| pg_restore | 17 |
| psql | 17 |

**MySQL development libraries**

| Dependency |
| :---------- |
| libmysqlclient-dev |

**Oracle Instant Client (required versions)**

Install the exact version of all Oracle Instant Client packages as follows:

| Dependency | Required Version |
| :--------- | :--------------- |
| [oracle-instantclient-basic](https://downloads.yugabyte.com/repos/apt/pool/main/oracle-instantclient-basic_21.5.0.0.0-1_amd64.deb) | 21.5.0.0.0 |
| [oracle-instantclient-devel](https://downloads.yugabyte.com/repos/apt/pool/main/oracle-instantclient-devel_21.5.0.0.0-1_amd64.deb) | 21.5.0.0.0 |
| [oracle-instantclient-jdbc](https://downloads.yugabyte.com/repos/apt/pool/main/oracle-instantclient-jdbc_21.5.0.0.0-1_amd64.deb) | 21.5.0.0.0 |
| [oracle-instantclient-sqlplus](https://downloads.yugabyte.com/repos/apt/pool/main/oracle-instantclient-sqlplus_21.5.0.0.0-1_amd64.deb) | 21.5.0.0.0 |
| [oracle-instantclient-tools](https://downloads.yugabyte.com/repos/apt/pool/main/oracle-instantclient-tools_21.5.0.0.0-1_amd64.deb) | 21.5.0.0.0 |

### Run the installer script

1. After installing all the dependencies, run the installer script on the airgapped machine:

    ```sh
    ./install-voyager-airgapped.sh
    ```

    The script:

    - Verifies that all required dependencies are installed.
    - Reports any missing dependencies and exits if any are not found.
    - Installs Ora2pg, Debezium, and YugabyteDB Voyager.

    **Script usage**

    ```sh
    ./install-voyager-airgapped.sh [options]
    ```

    | Argument | Description |
    | :------- | :---------- |
    | -d, --check-dependencies-only | Check dependencies only and exit without installing |
    | -f, --force-install | Install packages without checking dependencies |
    | -p, --pg-only | Check/install only PostgreSQL-related dependencies |
    | -m, --mysql-only | Check/install only MySQL-related dependencies |
    | -o, --oracle-only | Check/install only Oracle-related dependencies |
    | -h, --help | Display help message |

    Only one of `--pg-only`, `--mysql-only`, or `--oracle-only` can be specified at a time. If none are provided, the script checks dependencies for all database types.

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```

   The components installed by the script are described in the following table:

    | Component | Version |
    | :-------- | :------ |
    | YugabyteDB Voyager | \<voyager_version\> |
    | Debezium | 2.5.2-\<voyager_version\> |
    | Ora2pg | 23.2-yb.2 |

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
