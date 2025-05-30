<!--
+++
private=true
+++
-->

{{< note title = "Migrating from MySQL/Oracle on macOS" >}}

The brew install on macOS does not support installing ora2pg, which is required for MySQL/Oracle database schema export. If you are planning to migrate MySQL or Oracle source databases on macOS, install yb-voyager using Docker instead.

{{< /note >}}

Perform the following steps to install yb-voyager using brew for macOS:

1. [Tap](https://docs.brew.sh/Taps) the `yugabyte` Homebrew repository using the following command:

    ```sh
    brew tap yugabyte/tap
    ```

    The repository contains the formula to build and install `yb-voyager` on your macOS device.

    Note that the tap `yugabyte/yugabytedb` has been updated to `yugabyte/tap`. If you have previously installed yb-voyager using the tap `yugabyte/yugabytedb`, untap the entry using `brew untap yugabyte/yugabytedb`, and then tap using the preceding command.

1. Install the `postgresql@16` package to access `pg_dump` or `pg_restore` using the following command:

    ```sh
    brew install postgresql@16
    ```

1. Install `yb-voyager` and its dependencies using the following command:

    ```sh
    brew install yb-voyager
    ```

    {{< note >}}

Install a specific version of `yb-voyager` using the following command:

    brew install yb-voyager@<VERSION>

    {{< /note >}}

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```
