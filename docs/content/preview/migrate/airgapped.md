<!--
+++
private=true
+++
-->

You can perform an airgapped installation on RHEL 7/8, CentOS 7/8, and Ubuntu OS.

{{< tabpane text=true >}}

{{% tab header="Docker" %}}

Install yb-voyager using a Docker image in an airgapped environment using the following steps:

1. From a machine connected to the internet, run the following commands to pull and save the latest yb-voyager docker image:

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

You need to download the tarball containing all the rpm files that are necessary for installing and running `yb-voyager` on a machine with an internet connection. Transfer the downloaded files to your airgapped machine and proceed with the installation using the following steps:

1. Download the tarball containing all the rpm files on a machine with internet connection using the following command:

    ```sh
    # For RHEL 7 or CentOS 7
    wget https://downloads.yugabyte.com/repos/airgapped/airgapped-rhel7.tar.gz
    ```

    ```sh
    # For RHEL 8 or CentOS 8
    wget https://downloads.yugabyte.com/repos/airgapped/airgapped-rhel8.tar.gz
    ```

    ```sh
    # For Ubuntu
    wget https://downloads.yugabyte.com/repos/airgapped/airgapped_ubuntu.tar.gz
    ```

1. Transfer the tarball to your airgapped machine.

1. Unzip the folder on your airgapped machine. Change directory to the unzipped folder and install the rpm files using the following command:

    ```sh
    # For RHEL 7/8 or CentOS 7/8
    sudo yum install *
    ```

    ```sh
    # For Ubuntu
    sudo apt-get install ./*.deb
    ```

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```

{{% /tab %}}

{{< /tabpane >}}
