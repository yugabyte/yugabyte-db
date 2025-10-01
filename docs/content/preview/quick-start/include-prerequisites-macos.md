<!--
+++
private = true
block_indexing = true
+++
-->

Before installing YugabyteDB, ensure that you have the following available:

- <i class="fa-brands fa-apple" aria-hidden="true"></i> macOS 10.12 or later. If you are on Apple silicon, you need to download the macOS ARM package.

1. Python 3.11. To check the version, execute the following command:

    ```sh
    python --version
    ```

- `wget` or `curl`.

    Note that the following instructions use the `wget` command to download files. If you prefer to use `curl` (included in macOS), you can replace `wget` with `curl -O`.

    To install `wget` on your Mac, you can run the following command if you use Homebrew:

    ```sh
    brew install wget
    ```
