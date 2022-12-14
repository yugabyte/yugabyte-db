

You can perform an airgapped installation on RHEL 7/8, CentOS 7/8, and Ubuntu OS.

For Airgapped installations, you need to download all the rpm files that are necessary for installing and running `yb-voyager` on a machine with an internet connection.

Ensure that the OS of your current machine and the Airgapped machine is the _same_ so that you can download the correct versions of the rpm files.

1. Download the tarball containing all the rpm files on a machine with internet connection using the following command:

    ```sh
    // For RHEL 7 or CentOS 7
    wget https://downloads.yugabyte.com/repos/airgapped/airgapped-rhel7.tar.gz
    ```

    ```sh
    // For RHEL 8 or CentOS 8
    wget https://downloads.yugabyte.com/repos/airgapped/airgapped-rhel8.tar.gz
    ```

    ```sh
    // For Ubuntu
    wget https://downloads.yugabyte.com/repos/airgapped/airgapped_ubuntu.tar.gz
    ```

1. Now you can scp the tarball to your air-gapped machine.

1. Unzip the folder on your airgapped machine. Change directory to the unzipped folder and install the rpm files using the following command:

    ```sh
    // For RHEL 7/8 or CentOS 7/8
    sudo yum install *
    ```

    ```sh
    // For Ubuntu
    sudo apt-get install ./*.deb
    ```
