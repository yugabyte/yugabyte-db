<!--
+++
private=true
+++
-->

You can perform an airgapped installation on RHEL 7/8 and CentOS 7/8.

{{< tabpane text=true >}}

{{% tab header="Docker" %}}

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

{{< note title = "Package dependencies" >}}

To resolve package dependencies, yum takes into account the list of packages (and their versions) already installed on a machine.

For yum to download all the required dependencies, ensure that the list of *all* packages (and their versions) already installed on the airgapped machine and the connected machine are *exactly the same*. For example, it will not work if you prepare the installer bundle on RHEL 7.5 and try to install it on RHEL 7.2.

{{< /note >}}

1. Download rpm files for `yb-voyager` and its dependencies on a machine with internet connection using the following steps:

    1. Install the `yugabyte` yum repository on your machine using the following command:

        ```sh
        sudo yum install https://s3.us-west-2.amazonaws.com/downloads.yugabyte.com/repos/reporpms/yb-yum-repo-1.1-0.noarch.rpm
        ```

        This repository contains the yb-voyager rpm and other dependencies required to run `yb-voyager`.

    1. Install the `epel-release` repository using the following command:

        ```sh
        # For RHEL 7/CentOS 7
        sudo yum install https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm
        ```

        ```sh
        # For RHEL 8/CentOS 8
        sudo yum install https://dl.fedoraproject.org/pub/epel/epel-release-latest-8.noarch.rpm
        ```

    1. Install the PostgreSQL and Oracle instant clients repositories using the following command:

        ```sh
        sudo yum install pgdg-redhat-repo oracle-instant-clients-repo
        ```

        These repositories contain the rest of the dependencies required to run `yb-voyager`.

    1. If you're using **RHEL 8** or **CentOS 8**, do the following:

        - Disable the default `PostgreSQL` yum module on your machine using the following command:

            ```sh
            sudo dnf -qy module disable postgresql
            ```

        - Download rpm files for `perl-open` on your machine using the following command:

            ```sh
            sudo yum install --downloadonly --downloaddir=<path_to_directory> perl-open.noarch
            ```

    1. Download the rpm files for `yb-voyager` and its dependencies using the following command:

        ```sh
        sudo yum install --downloadonly --downloaddir=<path_to_directory> yb-voyager
        ```

1. Transfer the downloaded files to your airgapped machine.

1. Navigate to the folder containing all the files and install the rpm files using the following command:

    ```sh
    sudo yum install *
    ```

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```

{{% /tab %}}

{{< /tabpane >}}
