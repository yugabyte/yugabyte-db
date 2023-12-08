<!--
+++
private=true
+++
-->

Perform the following steps to install yb-voyager using apt for Ubuntu:

{{< note title="Note" >}}
`apt` installation is only supported for Ubuntu 22. For other versions such as 18 and 20, use the install script via the Source installation option.
{{< /note >}}

1. Install the Yugabyte apt repository on your machine using the following command:

    ```sh
    wget https://s3.us-west-2.amazonaws.com/downloads.yugabyte.com/repos/reporpms/yb-apt-repo_1.0.0_all.deb
    sudo apt-get install ./yb-apt-repo_1.0.0_all.deb
    ```

    This repository contains the `yb-voyager` debian package and the dependencies required to run `yb-voyager`.

1. Clean the `apt-get` cache and package lists using the following commands::

    ```sh
    sudo apt-get clean
    sudo apt-get update
    ```

1. Install yb-voyager and its dependencies using the following command:

    ```sh
    sudo apt-get install yb-voyager
    ```

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```
