## Prerequisites

1. One of the following operating systems

    <i class="icon-centos"></i> CentOS 7

    <i class="icon-ubuntu"></i> Ubuntu 16.04+

2. Verify that you have Python 2 installed. Support for Python 3 is in the works — to follow the status, see [Enhance yb-ctl and yb-docker-ctl to support Python3 #3025](https://github.com/yugabyte/yugabyte-db/issues/3025).

    ```sh
    $ python --version
    ```

    ```
    Python 2.7.10
    ```

3. `wget` or `curl` is available.

    The instructions use the `wget` command to download files. If you prefer to use `curl`, you can replace `wget` with `curl -O`.

    To install `wget`:

    - CentOS: `yum install wget`
    - Ubuntu: `apt install wget`

    To install `curl`:

    - CentOS: `yum install curl`
    - Ubuntu: `apt install curl`

## Download

1. Download the YugabyteDB package using the following `wget` command.

    ```sh
    $ wget https://downloads.yugabyte.com/yugabyte-2.0.10.0-linux.tar.gz
    ```

2. Extract the YugabyteDB package and then change directories to the YugabyteDB home.

    ```sh
    $ tar xvfz yugabyte-2.0.10.0-linux.tar.gz && cd yugabyte-2.0.10.0/
    ```

## Configure

To configure YugabyteDB, run the following shell script.

```sh
$ ./bin/post_install.sh
```
