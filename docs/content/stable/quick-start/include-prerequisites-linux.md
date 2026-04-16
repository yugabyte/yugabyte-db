<!--
+++
private = true
block_indexing = true
+++
-->

Before installing YugabyteDB, ensure that you have the following available:

- One of the [supported operating systems](/stable/reference/configuration/operating-systems/).

- Python 3.11. To check the version, execute the following command:

    ```sh
    python --version
    ```

    Starting from Ubuntu 20.04, `python` is no longer available. To fix this, run `sudo apt install python-is-python3`.

- `wget` or `curl`.

    The instructions use the `wget` command to download files. If you prefer to use `curl`, you can replace `wget` with `curl -O`.

    To install `wget`:

    - On CentOS, run `yum install wget`
    - On Ubuntu, run `apt install wget`

    To install `curl`:

    - On CentOS, run `yum install curl`
    - On Ubuntu, run `apt install curl`
