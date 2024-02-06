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

    Note that if you have already installed yb-voyager with tap `yugabyte/yugabytedb`, please untap the entry using `brew untap yugabyte/yugabytedb`, and then tap using the preceding command as the tap `yugabyte/yugabytedb` is updated to `yugabyte/tap`.

1. Install `yb-voyager` and its dependencies using the following command:

    ```sh
    brew install yb-voyager
    ```

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```
