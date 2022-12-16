<!--
+++
private=true
+++
-->

Perform the following steps to install yb-voyager using apt for Ubuntu:

1. Install the Yugabyte apt repository on your machine using the following command:

    ```sh
    wget https://s3.us-west-2.amazonaws.com/downloads.yugabyte.com/repos/reporpms/yb-apt-repo_1.0.0_all.deb
    sudo apt-get install ./yb-apt-repo_1.0.0_all.deb
    ```

    This repository contains the `yb-voyager` debian package and the dependencies required to run `yb-voyager`.

2. Clean out the temporary cache of `apt-get` and update the package lists of `apt-get` on your machine using the following commands:

    ```sh
    sudo apt-get clean
    sudo apt-get update
    ```

3. Install yb-voyager and its dependencies using the following command:

    ```sh
    sudo apt-get install yb-voyager
    ```

