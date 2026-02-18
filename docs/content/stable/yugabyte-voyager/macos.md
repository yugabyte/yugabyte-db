<!--
+++
private=true
+++
-->

{{< note title = "Migrating from MySQL/Oracle on macOS" >}}

The brew install on macOS does not support installing ora2pg, which is required for MySQL/Oracle database schema export. If you are planning to migrate MySQL or Oracle source databases on macOS, install yb-voyager using Docker instead.

{{< /note >}}

### Dependencies

When installing YugabyteDB Voyager using Homebrew, the following dependencies are installed automatically as part of the formula.

- yugabyte/tap/debezium@2.5.2-\<voyager_version\>
- postgresql@17
- sqlite
- go@1.24 (build-time)

Perform the following steps to install yb-voyager using brew for macOS:

1. [Tap](https://docs.brew.sh/Taps) the `yugabyte` Homebrew repository using the following command:

    ```sh
    brew tap yugabyte/tap
    ```

    The repository contains the formula to build and install `yb-voyager` on your macOS device.

    Note that the tap `yugabyte/yugabytedb` has been updated to `yugabyte/tap`. If you have previously installed yb-voyager using the tap `yugabyte/yugabytedb`, untap the entry using `brew untap yugabyte/yugabytedb`, and then tap using the preceding command.

1. Install PostgreSQL 17 (required for `pg_dump` and `pg_restore`) using Homebrew:

    ```sh
    brew install postgresql@17
    ```

    After installing PostgreSQL 17, Homebrew displays instructions on how to add the PostgreSQL 17 binaries to your shell's PATH.
    These steps vary depending on your system's Homebrew installation path (for example: `/opt/homebrew` or `/usr/local`) and the shell you are using.

      1. Update your PATH by following the _exact_ instructions shown by Homebrew. Following is an example of what Homebrew may display:

          ```output
          If you need to have postgresql@17 first in your PATH, run:
          echo 'export PATH="/opt/homebrew/opt/postgresql@17/bin:$PATH"' >> ~/.zshrc
          For compilers to find postgresql@17 you may need to set:
          export LDFLAGS="-L/opt/homebrew/opt/postgresql@17/lib"
          export CPPFLAGS="-I/opt/homebrew/opt/postgresql@17/include"
          ```

      1. After the update, restart your terminal and verify that both report to PostgreSQL 17:

          ```sh
          pg_dump --version
          pg_restore --version
          ```

1. Install `yb-voyager` and its dependencies using the following command:

    ```sh
    brew install yb-voyager
    ```

    To install a specific version of `yb-voyager`, use the following command:

    ```sh
    brew install yb-voyager@<VERSION>
    ```

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```
