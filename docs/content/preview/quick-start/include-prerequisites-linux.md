<!--
+++
private = true
+++
-->

Before installing YugabyteDB, ensure that you have the following available:

1. One of the [supported operating systems](/preview/reference/configuration/operating-systems/).

1. Python 3. To check the version, execute the following command:

    ```sh
    python --version
    ```

    ```output
    Python 3.7.3
    ```

    By default, CentOS 8 does not have an unversioned system-wide `python` command. To fix this, set `python3` as the alternative for `python` by running `sudo alternatives --set python /usr/bin/python3`.

    Starting from Ubuntu 20.04, `python` is no longer available. To fix this, run `sudo apt install python-is-python3`.

1. `wget` or `curl`.

    The instructions use the `wget` command to download files. If you prefer to use `curl`, you can replace `wget` with `curl -O`.

    To install `wget`:

    - On CentOS, run `yum install wget`
    - On Ubuntu, run `apt install wget`

    To install `curl`:

    - On CentOS, run `yum install curl`
    - On Ubuntu, run `apt install curl`
