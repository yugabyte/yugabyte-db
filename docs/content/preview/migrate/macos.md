<!--
+++
private=true
+++
-->

{{< note title = "Note on migrating from MySQL/Oracle on MacOS" >}}

To migrate from MySQL/Oracle source databases on MacOS to YugabyteDB, install yb-voyager using docker based instructions.

{{< /note >}}

Perform the following steps to install yb-voyager using brew for MacOS:

1. [Tap](https://docs.brew.sh/Taps) to the `yugabyte` brew repository using the following command:

    ```sh
    brew tap yugabyte/yugabytedb
    ```

    The repository contains the formula to build and install `yb-voyager` on your MacOS device.

1. Install `yb-voyager` and its dependencies using the following command:

    ```sh
    brew install yb-voyager
    ```

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```
