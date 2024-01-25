<!--
+++
private=true
+++
-->

Perform the following steps to install yb-voyager using yum for RHEL 7/8 and CentOS 7/8:

1. Update the yum package manager, and all the packages and repositories installed on your machine using the following command:

    ```sh
    sudo yum update
    ```

1. Install the `yugabyte` yum repository using the following command:

    ```sh
    sudo yum install https://s3.us-west-2.amazonaws.com/downloads.yugabyte.com/repos/reporpms/yb-yum-repo-1.1-0.noarch.rpm
    ```

    This repository contains the yb-voyager rpm and other dependencies required to run `yb-voyager`.

1. Install the `epel-release` repository using the following command:

    ```sh
    # For RHEL 7 or CentOS 7
    sudo yum install https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm
    ```

    ```sh
    # For RHEL8 or Centos 8
    sudo yum install https://dl.fedoraproject.org/pub/epel/epel-release-latest-8.noarch.rpm
    ```

1. Install the PostgreSQL and Oracle instant clients repositories using the following command:

    ```sh
    sudo yum install pgdg-redhat-repo oracle-instant-clients-repo
    ```

    These repositories contain the rest of the dependencies required to run `yb-voyager`.

    {{< note >}}
Note that if you're using **RHEL 8** or **CentOS 8**, perform the following two steps before proceeding to step 5.

- Disable the default `PostgreSQL` yum module on your machine using the following command:

    ```sh
    sudo dnf --disablerepo=* -y install https://download.postgresql.org/pub/repos/yum/reporpms/EL-8-ppc64le/pgdg-redhat-repo-latest.noarch.rpm
    ```

- Install `perl-open` on your machine using the following command:

    ```sh
    sudo yum install perl-open.noarch
    ```

    {{< /note >}}

1. Update the yum package manager and all the packages and repositories installed on your machine using the following command:

    ```sh
    sudo yum update
    ```

1. Install `yb-voyager` and its dependencies using the following command:

    ```sh
    sudo yum install yb-voyager
    ```

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```
