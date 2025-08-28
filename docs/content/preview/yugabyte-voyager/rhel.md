<!--
+++
private=true
+++
-->

{{< tabpane text=true >}}

{{% tab header="RHEL 8" lang="rhel8" %}}

Perform the following steps to install yb-voyager using yum for RHEL 8 and CentOS 8:

1. Update the yum package manager, and all the packages and repositories installed on your machine using the following command:

    ```sh
    sudo yum update
    ```

1. Install the `yugabyte` yum repository using the following command:

    ```sh
    sudo yum install https://software.yugabyte.com/repos/reporpms/rhel-8/yb-yum-repo-1.1-0.noarch.rpm
    ```

    This repository contains the yb-voyager rpm and other dependencies required to run `yb-voyager`.

1. Install the `epel-release` repository using the following command:

    ```sh
    sudo yum install https://dl.fedoraproject.org/pub/epel/epel-release-latest-8.noarch.rpm
    ```

1. Install the Oracle instant client repositories using the following command:

    ```sh
    sudo yum install oracle-instant-clients-repo
    ```

1. Install the PostgreSQL repositories using the following command:

    ```sh
    sudo yum --disablerepo=* -y install https://download.postgresql.org/pub/repos/yum/reporpms/EL-8-x86_64/pgdg-redhat-repo-latest.noarch.rpm
    ```

    These repositories contain the rest of the dependencies required to run `yb-voyager`.

1. Disable the default `PostgreSQL` yum module on your machine using the following command:

    ```sh
    sudo dnf -qy module disable postgresql
    ```

1. Install `perl-open` on your machine using the following command:

    ```sh
    sudo yum install perl-open.noarch
    ```

1. Update the yum package manager and all the packages and repositories installed on your machine using the following command:

    ```sh
    sudo yum update
    ```

1. Install `yb-voyager` and its dependencies using the following command:

    ```sh
    sudo yum install yb-voyager
    ```

   To install a specific version of `yb-voyager` on your machine, use the following command:

    ```sh
    sudo yum install yb-voyager-<VERSION>
    ```

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```

### Upgrade yb-voyager

Upgrade yb-voyager using the following command:

```sh
sudo yum update yb-voyager
```

{{% /tab %}}

{{% tab header="RHEL 9" lang="rhel9" %}}

Perform the following steps to install yb-voyager using yum for RHEL 9 and CentOS 9:

1. Update the yum package manager, and all the packages and repositories installed on your machine using the following command:

    ```sh
    sudo dnf update
    ```

1. Install the `yugabyte` yum repository using the following command:

    ```sh
    sudo dnf install https://software.yugabyte.com/repos/reporpms/rhel-9/yb-yum-repo-1.1-0.noarch.rpm -y
    ```

1. Install the `epel-release` repository using the following command:

    ```sh
    sudo dnf install https://dl.fedoraproject.org/pub/epel/epel-release-latest-9.noarch.rpm -y
    ```

1. Install `mysql-community-release` repository using the following command:

    ```sh
    sudo dnf install https://dev.mysql.com/get/mysql84-community-release-el9-1.noarch.rpm -y
    ```

1. Install the PostgreSQL repositories using the following command:

    ```sh
    sudo dnf --disablerepo=* install https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-x86_64/pgdg-redhat-repo-latest.noarch.rpm -y
    ```

    These repositories contain the rest of the dependencies required to run `yb-voyager`.

1. Disable the default `PostgreSQL` yum module on your machine using the following command:

    ```sh
    sudo dnf -qy module disable postgresql
    ```

1. Install `perl-open` on your machine using the following command:

    ```sh
    sudo dnf install perl-open.noarch -y
    ```

1. Install Oracle Instant Clients using the following command:

    ```sh
    OIC_URL="https://download.oracle.com/otn_software/linux/instantclient/215000" && \
    sudo dnf install -y \
        ${OIC_URL}/oracle-instantclient-tools-21.5.0.0.0-1.x86_64.rpm \
        ${OIC_URL}/oracle-instantclient-basic-21.5.0.0.0-1.x86_64.rpm \
        ${OIC_URL}/oracle-instantclient-devel-21.5.0.0.0-1.x86_64.rpm \
        ${OIC_URL}/oracle-instantclient-jdbc-21.5.0.0.0-1.x86_64.rpm \
        ${OIC_URL}/oracle-instantclient-sqlplus-21.5.0.0.0-1.x86_64.rpm
    ```

1. Update the yum package manager and all the packages and repositories installed on your machine using the following command:

    ```sh
    sudo dnf update
    ```

1. Install `yb-voyager` and its dependencies using the following command:

    ```sh
    sudo dnf install yb-voyager
    ```

    To install a specific version of `yb-voyager` on your machine, use the following command:

    ```sh
    sudo dnf install yb-voyager-<VERSION>
    ```

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```

### Upgrade yb-voyager

Upgrade yb-voyager using the following command:

```sh
sudo yum update yb-voyager
```

{{% /tab %}}

{{< /tabpane >}}
