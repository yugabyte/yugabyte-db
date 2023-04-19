<!--
+++
private=true
+++
-->

{{< note title = "Note on migrating from MySQL/Oracle on macOS" >}}

To migrate from MySQL/Oracle source databases on macOS to YugabyteDB, install yb-voyager using docker-based instructions.

{{< /note >}}

Perform the following steps to install yb-voyager using brew for macOS:

1. [Tap](https://docs.brew.sh/Taps) the `yugabyte` Homebrew repository using the following command:

    ```sh
    brew tap yugabyte/tap
    ```

    The repository contains the formula to build and install `yb-voyager` on your macOS device.

1. Install `yb-voyager` and its dependencies using the following command:

    ```sh
    brew install yb-voyager
    ```

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```
