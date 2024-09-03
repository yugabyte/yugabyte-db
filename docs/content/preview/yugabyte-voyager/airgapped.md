<!--
+++
private=true
+++
-->

{{< tabpane text=true >}}

{{% tab header="Docker" %}}

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
    wget -O ./yb-voyager https://raw.githubusercontent.com/yugabyte/yb-voyager/main/docker/yb-voyager-docker
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

{{% tab header="Yum" %}}

You can perform an airgapped installation on RHEL 8 and CentOS 8.

1. Download the airgapped bundle:

    ```sh
    wget https://s3.us-west-2.amazonaws.com/downloads.yugabyte.com/repos/airgapped/yb-voyager-1.8.0-rhel-8-x86_64.tar.gz
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

### Dependencies for RHEL and CentOS 8

Binutils: Minimum version: 2.25

Java: Minimum version: 17

pg_dump: Minimum version: 14

pg_restore: Minimum version: 14

psql: Minimum version: 14

#### Yum packages

- gcc (no version dependency)
- make (no version dependency)
- sqlite (no version dependency)
- perl (no version dependency)
- perl-DBI (no version dependency)
- perl-App-cpanminus (no version dependency)
- perl-ExtUtils-MakeMaker (no version dependency)
- mysql-devel (no version dependency)
- oracle-instantclient-tools with exact version 21.5.0.0.0
- oracle-instantclient-basic with exact version 21.5.0.0.0
- oracle-instantclient-devel with exact version 21.5.0.0.0
- oracle-instantclient-jdbc with exact version 21.5.0.0.0
- oracle-instantclient-sqlplus with exact version 21.5.0.0.0

#### CPAN modules

- DBD::mysql with minimum version 5.005
- Test::NoWarnings with minimum version 1.06
- DBD::Oracle with minimum version 1.83
- String::Random (no version dependency)
- IO::Compress::Base (no version dependency)

### Installation Script

The script by default checks what dependencies are installed on the system and throws an error mentioning the missing dependencies. If all the dependencies are found to be installed, it proceeds with the installation of ora2pg, debezium, and yb-voyager.

Usage:

```sh
./install-voyager-airgapped.sh [options]
```

The options are as follows.

| Argument | Description/valid options |
| :------- | :------------------------ |
| -d, --check-only-dependencies | Check only dependencies and exit. |
| -f, --force-install | Force install packages without checking dependencies. |
| -h, --help | Display this help message. |

### Oracle Instant Client installation help for Centos/RHEL

You can download the oracle instant client rpms from the following links:

- [oracle-instantclient-tools](https://download.oracle.com/otn_software/linux/instantclient/215000/oracle-instantclient-tools-21.5.0.0.0-1.x86_64.rpm)

- [oracle-instantclient-basic](https://download.oracle.com/otn_software/linux/instantclient/215000/oracle-instantclient-basic-21.5.0.0.0-1.x86_64.rpm)

- [oracle-instantclient-devel](https://download.oracle.com/otn_software/linux/instantclient/215000/oracle-instantclient-devel-21.5.0.0.0-1.x86_64.rpm)

- [oracle-instantclient-jdbc](https://download.oracle.com/otn_software/linux/instantclient/215000/oracle-instantclient-jdbc-21.5.0.0.0-1.x86_64.rpm)

- [oracle-instantclient-sqlplus](https://download.oracle.com/otn_software/linux/instantclient/215000/oracle-instantclient-sqlplus-21.5.0.0.0-1.x86_64.rpm)

{{% /tab %}}

{{% tab header="Ubuntu" %}}

You can perform an airgapped installation on Ubhuntu 22 and leter.

1. Download the airgapped bundle:

    ```sh
    wget https://s3.us-west-2.amazonaws.com/downloads.yugabyte.com/repos/airgapped/yb-voyager-1.8.0_debian.tar.gz
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

- gcc (no version dependency)
- sqlite3 (no version dependency)
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

#### CPAN modules

- DBD::mysql with minimum version 5.005
- Test::NoWarnings with minimum version 1.06
- DBD::Oracle with minimum version 1.83
- String::Random (no version dependency)
- IO::Compress::Base (no version dependency)

### Install script

The script by default checks what dependencies are installed on the system and throws an error mentioning the missing dependencies. If all the dependencies are found to be installed, it proceeds with the installation of ora2pg, debezium, and yb-voyager.

Usage:

```sh
./install-voyager-airgapped.sh [options]
```

The options are as follows.

| Argument | Description/valid options |
| :------- | :------------------------ |
| -d, --check-only-dependencies | Check only dependencies and exit. |
| -f, --force-install | Force install packages without checking dependencies. |
| -h, --help | Display this help message. |

### Oracle Instant Client installation help for Ubuntu

You can download the oracle instant client rpms from the following links:

- [oracle-instantclient-tools](https://s3.us-west-2.amazonaws.com/downloads.yugabyte.com/repos/apt/pool/main/oracle-instantclient-tools_21.5.0.0.0-1_amd64.debrpm)

- [oracle-instantclient-basic](https://s3.us-west-2.amazonaws.com/downloads.yugabyte.com/repos/apt/pool/main/oracle-instantclient-basic_21.5.0.0.0-1_amd64.deb)

- [oracle-instantclient-devel](https://s3.us-west-2.amazonaws.com/downloads.yugabyte.com/repos/apt/pool/main/oracle-instantclient-devel_21.5.0.0.0-1_amd64.deb)

- [oracle-instantclient-jdbc](https://s3.us-west-2.amazonaws.com/downloads.yugabyte.com/repos/apt/pool/main/oracle-instantclient-jdbc_21.5.0.0.0-1_amd64.deb)

- [oracle-instantclient-sqlplus](https://s3.us-west-2.amazonaws.com/downloads.yugabyte.com/repos/apt/pool/main/oracle-instantclient-sqlplus_21.5.0.0.0-1_amd64.deb)

{{% /tab %}}

{{< /tabpane >}}
